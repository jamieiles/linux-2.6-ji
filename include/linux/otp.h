/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This driver implements a user interface for reading and writing OTP
 * memory. This OTP can be used for executing secure boot code or for the
 * secure storage of keys and any other user data.  We support multiple
 * backends for different OTP macros.
 *
 * The OTP is configured through sysfs and is read and written through device
 * nodes. The top level OTP device gains write_enable, num_regions, and
 * strict_programming attributes. We also create an otp bus to which we add a
 * device per region. The OTP can supports multiple regions and when we divide
 * the regions down we create a new child device on the otp bus. This child
 * device has format and size attributes.
 *
 * To update the number of regions, the format of a region or to program a
 * region, the write_enable attribute of the OTP device must be set to
 * "enabled".
 */
#ifndef __OTP_H__
#define __OTP_H__

#include <linux/types.h>

#ifdef __KERNEL__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>

enum otp_redundancy_fmt {
	OTP_REDUNDANCY_FMT_SINGLE_ENDED,
	OTP_REDUNDANCY_FMT_REDUNDANT,
	OTP_REDUNDANCY_FMT_DIFFERENTIAL,
	OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT,
	OTP_REDUNDANCY_FMT_ECC,
	OTP_REDUNDANCY_NR_FMTS,
};

struct otp_device;
struct otp_region;

/**
 * struct otp_device_ops - operations for the OTP device.
 *
 * @name:		The name of the driver backend.
 * @owner:		The owning module.
 * @get_nr_regions:	Get the number of regions that the OTP is partitioned
 *			into.  Note that this is the number of regions in the
 *			device, not the number of regions registered.
 * @set_nr_regions:	Increase the number of partitions in the device.
 *			Returns zero on success, negative errno on failure.
 * @set_fmt:		Set the read-mode redundancy for the region.  The OTP
 *			devices need to be put into the right redundancy mode
 *			before reading/writing.
 * @write_word:		Write a 64-bit word to the OTP.
 * @read_word:		Read a 64-bit word from the OTP.
 * @lock_word:		Lock a word to prevent further writes.  Optional for
 *			some OTP devices.
 */
struct otp_device_ops {
	const char	*name;
	struct module	*owner;
	ssize_t		(*get_nr_regions)(struct otp_device *dev);
	int		(*set_nr_regions)(struct otp_device *dev,
					  int nr_regions);
	int		(*set_fmt)(struct otp_device *dev,
				   enum otp_redundancy_fmt fmt);
	int		(*write_word)(struct otp_device *dev,
				      struct otp_region *region,
				      unsigned long word_addr, u64 value);
	int		(*read_word)(struct otp_device *dev,
				     struct otp_region *region,
				     unsigned long word_addr, u64 *value);
	long		(*lock_word)(struct otp_device *dev,
				     struct otp_region *region,
				     unsigned long word_addr);
};

/**
 * enum otp_device_caps - Flags to indicate capabilities for the OTP device.
 *
 * @OTP_CAPS_NO_SUBWORD_WRITE:	only full word sized writes may be performed.
 *				Don't use read-modify-write cycles for
 *				performing unaligned writes.
 */
enum otp_device_caps {
	OTP_CAPS_NO_SUBWORD_WRITE	= (1 << 0),
};

/**
 * struct otp_device - a picoxcell OTP device.
 *
 * @ops:		The operations to use for manipulating the device.
 * @dev:		The parent device (typically a platform_device) that
 *			provides the OTP.
 * @regions:		The regions registered to the device.
 * @size:		The size of the OTP in bytes.
 * @max_regions:	The maximum number of regions the device may have.
 * @dev_nr:		The OTP device ID, used for creating the otp%c
 *			identifier.
 * @flags:		Flags to indicate features of the OTP that the upper
 *			layer should handle.
 * @word_sz:		The size of the words of storage in the OTP (in
 *			bytes).
 */
struct otp_device {
	struct mutex			lock;
	int				write_enable;
	int				strict_programming;
	const struct otp_device_ops	*ops;
	struct device			dev;
	struct list_head		regions;
	size_t				size;
	dev_t				devno;
	unsigned			max_regions;
	int				dev_nr;
	size_t				word_sz;
	unsigned long			flags;
};

static inline void *otp_dev_get_drvdata(struct otp_device *otp_dev)
{
	return dev_get_drvdata(&otp_dev->dev);
}

static inline void otp_dev_set_drvdata(struct otp_device *otp_dev,
				       void *data)
{
	dev_set_drvdata(&otp_dev->dev, data);
}

/**
 * struct otp_region_ops - operations to manipulate OTP regions.
 *
 * @set_fmt:		Permanently set the format of the region.  Returns
 *			zero on success.
 * @get_fmt:		Get the redundancy format of the region.
 * @get_size:		Get the effective storage size of the region.
 *			Depending on the number of regions in the device and
 *			the redundancy format of the region, this may vary.
 */
struct otp_region_ops {
	int			(*set_fmt)(struct otp_region *region,
					   enum otp_redundancy_fmt fmt);
	enum otp_redundancy_fmt	(*get_fmt)(struct otp_region *region);
	ssize_t			(*get_size)(struct otp_region *region);
};

/**
 * struct otp_region: a single region of OTP.
 *
 * @ops:	Operations for manipulating the region.
 * @label:	The label of the region to export as a device attribute.
 * @dev:	The device to register with the driver model.
 * @cdev:	The character device used to provide userspace access to the
 *		region.
 * @head:	The position in the devices region list.
 * @region_nr:	The region number of the region.  Devices number their regions
 *		from 1 upwards.
 */
struct otp_region {
	const struct otp_region_ops	*ops;
	const char			*label;
	struct device			dev;
	struct cdev			cdev;
	struct list_head		head;
	unsigned			region_nr;
};

/**
 * otp_device_alloc - create a new OTP device.
 *
 * Returns the newly created OTP device on success or a ERR_PTR() encoded
 * errno on failure.  The new device is automatically registered and can be
 * released with otp_device_unregister().  This will increase the reference
 * count on dev.
 *
 * @dev:	The parent device that provides the OTP implementation.
 * @ops:	The operations to manipulate the OTP device.
 * @size:	The size, in bytes of the OTP device.
 * @word_sz:	The size of the words in the OTP memory.
 * @max_regions:The maximum number of regions in the device.
 * @flags:	Bitmask of enum otp_device_flags for the device.
 */
struct otp_device *otp_device_alloc(struct device *dev,
				    const struct otp_device_ops *ops,
				    size_t size, size_t word_sz,
				    unsigned max_regions,
				    unsigned long flags);

/**
 * otp_device_unregister - unregister an existing struct otp_device.
 *
 * This unregisters an otp_device and any regions that have been registered to
 * it.  Once all regions have been released, the parent reference count is
 * decremented and the otp_device will be freed.  Callers must assume that dev
 * is invalidated during this call.
 *
 * @dev:	The otp_device to unregister.
 */
void otp_device_unregister(struct otp_device *dev);

/**
 * otp_region_alloc - create and register a new OTP region.
 *
 * Create and register a new region in an existing device with specified
 * region operations.  Returns a pointer to the region on success, or an
 * ERR_PTR() encoded errno on failure.
 *
 * Note: this takes the OTP semaphore so may not be called from one of the
 * otp_device_ops or otp_region_ops callbacks or this may lead to deadlock.
 *
 * @dev:	The device to add the region to.
 * @ops:	The operations for the region.
 * @region_nr:	The region ID.  Must be unique for the region.
 * @fmt:	The format string for the region label.
 */
struct otp_region *otp_region_alloc(struct otp_device *dev,
				    const struct otp_region_ops *ops,
				    int region_nr, const char *fmt, ...);

/**
 * otp_region_alloc_unlocked - create and register a new OTP region.
 *
 * This is the same as otp_region_alloc() but does not take the OTP semaphore
 * so may only be called from inside one of the otp_device_ops or
 * otp_region_ops callbacks.
 *
 * @dev:	The device to add the region to.
 * @ops:	The operations for the region.
 * @region_nr:	The region ID.  Must be unique for the region.
 * @fmt:	The format string for the region label.
 */
struct otp_region *otp_region_alloc_unlocked(struct otp_device *dev,
					     const struct otp_region_ops *ops,
					     int region_nr, const char *fmt,
					     ...);

/**
 * otp_region_unregister - unregister a given OTP region.
 *
 * This unregisters a region from the device and forms part of
 * otp_device_unregister().
 *
 * @region:	The region to unregister.
 */
#define otp_region_unregister(region)	device_unregister(&(region)->dev)

/**
 * otp_strict_programming_enabled - check if we are in strict programming
 * mode.
 *
 * Some OTP devices support different redundancy modes.  These devices often
 * need multiple words programmed to represent a single word in that
 * redundancy format.  If strict programming is enabled then all of the
 * redundancy words must program correctly to indicate success.  If strict
 * programming is disabled then we will allow errors in the redundant word as
 * long as the contents of the whole word are read back correctly with the
 * required redundancy mode.
 *
 * @otp_dev:	The device to interrogate.
 */
bool otp_strict_programming_enabled(struct otp_device *otp_dev);

/**
 * otp_write_enabled - check whether writes are allowed to the device.
 */
bool otp_write_enabled(struct otp_device *otp_dev);

static inline struct otp_region *to_otp_region(struct device *dev)
{
	return container_of(dev, struct otp_region, dev);
}

static inline struct otp_device *to_otp_device(struct device *dev)
{
	return container_of(dev, struct otp_device, dev);
}

#endif /* __KERNEL__ */

/*
 * struct otp_lock_area_info - Lock an area of OTP memory in a given region.
 *
 * @byte_addr:	The byte offset from the beginning of the region.  This must
 *		be a multiple of the OTP word size.
 * @byte_count:	The number of bytes to lock down.  This must be a multiple of
 *		the OTP word size.  After the ioctl() completes this will be
 *		updated with the number of bytes that were actually locked.
 */
struct otp_lock_area_info {
	__u32		byte_addr;
	__u32		byte_count;
};
#define OTP_LOCK_AREA	_IOWR('O', 0x10, struct otp_lock_area_info)

#endif /* __OTP_H__ */

/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "otp: " fmt

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/otp.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

/* We'll allow OTP devices to be named otpa-otpz. */
#define MAX_OTP_DEVICES		26

static unsigned long registered_otp_map[BITS_TO_LONGS(MAX_OTP_DEVICES)];
static DEFINE_MUTEX(otp_register_mutex);

/*
 * The otp currently works in 64 bit words. When we are programming or
 * reading, everything is done with 64 bit word addresses.
 */
#define OTP_WORD_SIZE			8

bool otp_strict_programming_enabled(struct otp_device *otp_dev)
{
	return otp_dev->strict_programming;
}
EXPORT_SYMBOL_GPL(otp_strict_programming_enabled);

bool otp_write_enabled(struct otp_device *otp_dev)
{
#ifdef CONFIG_OTP_WRITE_ENABLE
	return otp_dev->write_enable;
#else /* CONFIG_OTP_WRITE_ENABLE */
	return false;
#endif /* CONFIG_OTP_WRITE_ENABLE */
}
EXPORT_SYMBOL_GPL(otp_write_enabled);

static const char *otp_format_names[OTP_REDUNDANCY_NR_FMTS] = {
	[OTP_REDUNDANCY_FMT_SINGLE_ENDED]	= "single-ended",
	[OTP_REDUNDANCY_FMT_REDUNDANT]		= "redundant",
	[OTP_REDUNDANCY_FMT_DIFFERENTIAL]	= "differential",
	[OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT] = "differential-redundant",
	[OTP_REDUNDANCY_FMT_ECC]		= "ecc",
};

static const char *otp_fmt_to_string(enum otp_redundancy_fmt fmt)
{
	if (fmt < 0 || fmt >= OTP_REDUNDANCY_NR_FMTS)
		return NULL;

	return otp_format_names[fmt];
}

static int otp_string_to_fmt(const char *name)
{
	int i;

	for (i = 0; i < OTP_REDUNDANCY_NR_FMTS; ++i)
		if (sysfs_streq(name, otp_format_names[i]))
			return i;

	return -1;
}

static ssize_t otp_dev_get_and_lock(struct otp_device *otp_dev)
{
	if (mutex_lock_interruptible(&otp_dev->lock))
		return -ERESTARTSYS;

	if (!try_module_get(otp_dev->ops->owner)) {
		mutex_unlock(&otp_dev->lock);
		return -ENODEV;
	}

	return 0;
}

static void otp_dev_put_and_unlock(struct otp_device *otp_dev)
{
	module_put(otp_dev->ops->owner);
	mutex_unlock(&otp_dev->lock);
}

static ssize_t otp_format_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct otp_region *region = to_otp_region(dev);
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	enum otp_redundancy_fmt fmt;
	const char *fmt_string;
	ssize_t err;

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;

	if (region->ops->get_fmt(region))
		fmt = region->ops->get_fmt(region);
	else
		fmt = OTP_REDUNDANCY_FMT_SINGLE_ENDED;

	otp_dev_put_and_unlock(otp_dev);

	fmt_string = otp_fmt_to_string(fmt);
	if (!fmt_string)
		return -EINVAL;

	return sprintf(buf, "%s\n", fmt_string);
}

static ssize_t otp_format_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	ssize_t err = 0;
	struct otp_region *region = to_otp_region(dev);
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	enum otp_redundancy_fmt new_fmt;

	if (!region->ops->set_fmt)
		return -EOPNOTSUPP;

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;

	/* This is irreversible so don't make it too easy to break it! */
	if (!otp_write_enabled(otp_dev)) {
		err = -EPERM;
		goto out;
	}

	new_fmt = otp_string_to_fmt(buf);
	if (new_fmt < 0) {
		err = -EINVAL;
		goto out;
	}
	region->ops->set_fmt(region, new_fmt);

out:
	otp_dev_put_and_unlock(otp_dev);

	return err ?: len;
}
static DEVICE_ATTR(format, S_IRUSR | S_IWUSR, otp_format_show,
		   otp_format_store);

static ssize_t otp_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct otp_region *region = to_otp_region(dev);
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	size_t region_sz;
	ssize_t err;

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;

	region_sz = region->ops->get_size(region);

	otp_dev_put_and_unlock(otp_dev);

	return sprintf(buf, "%zu\n", region_sz);
}
static DEVICE_ATTR(size, S_IRUSR, otp_size_show, NULL);

static ssize_t otp_label_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct otp_region *region = to_otp_region(dev);

	return sprintf(buf, "%s\n", region->label);
}
static DEVICE_ATTR(label, S_IRUSR, otp_label_show, NULL);

static struct bus_type otp_bus_type = {
	.name		= "otp",
};

static struct attribute *region_attrs[] = {
	&dev_attr_format.attr,
	&dev_attr_size.attr,
	&dev_attr_label.attr,
	NULL,
};

static const struct attribute_group region_attr_group = {
	.attrs		= region_attrs,
};

const struct attribute_group *region_attr_groups[] = {
	&region_attr_group,
	NULL,
};

static struct device_type region_type = {
	.name		= "region",
	.groups		= region_attr_groups,
};

static ssize_t otp_attr_store_enabled(struct device *dev, const char *buf,
				      size_t len, int *param)
{
	ssize_t err = 0;
	struct otp_device *otp_dev = to_otp_device(dev);

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;

	if (sysfs_streq(buf, "enabled"))
		*param = 1;
	else if (sysfs_streq(buf, "disabled"))
		*param = 0;
	else
		err = -EINVAL;

	otp_dev_put_and_unlock(otp_dev);

	return err ?: len;
}

static ssize_t otp_attr_show_enabled(struct device *dev, char *buf, int param)
{
	ssize_t ret;
	struct otp_device *otp_dev = to_otp_device(dev);

	ret = otp_dev_get_and_lock(otp_dev);
	if (ret)
		return ret;

	ret = sprintf(buf, "%s\n", param ? "enabled" : "disabled");

	otp_dev_put_and_unlock(otp_dev);

	return ret;
}

/*
 * Show the current write enable state of the otp. Users can only program the
 * otp when this shows 'enabled'.
 */
static ssize_t otp_we_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	return otp_attr_show_enabled(dev, buf, otp_dev->write_enable);
}

/*
 * Set the write enable state of the otp. 'enabled' will enable programming
 * and 'disabled' will prevent further programming from occurring. On loading
 * the module, this will default to 'disabled'.
 */
#ifdef CONFIG_OTP_WRITE_ENABLE
static ssize_t otp_we_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	return otp_attr_store_enabled(dev, buf, len, &otp_dev->write_enable);
}
#else
static ssize_t otp_we_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	if (!sysfs_streq(buf, "disabled"))
		return -EACCES;
	return len;
}
#endif
static DEVICE_ATTR(write_enable, S_IRUSR | S_IWUSR, otp_we_show, otp_we_store);

/*
 * Show the current strict programming state of the otp. If set to "enabled",
 * then when programming, all raw words must program correctly to succeed. If
 * disabled, then as long as the word reads back correctly in the redundant
 * mode, then some bits may be allowed to be incorrect in the raw words.
 */
static ssize_t otp_strict_programming_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	return otp_attr_show_enabled(dev, buf, otp_dev->strict_programming);
}

/*
 * Set the current strict programming state of the otp. If set to "enabled",
 * then when programming, all raw words must program correctly to succeed. If
 * disabled, then as long as the word reads back correctly in the redundant
 * mode, then some bits may be allowed to be incorrect in the raw words.
 */
static ssize_t otp_strict_programming_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	return otp_attr_store_enabled(dev, buf, len,
				      &otp_dev->strict_programming);
}
static DEVICE_ATTR(strict_programming, S_IRUSR | S_IWUSR,
		   otp_strict_programming_show, otp_strict_programming_store);

static ssize_t otp_num_regions_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct otp_device *otp_dev = to_otp_device(dev);
	ssize_t nr_regions, err;

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;
	nr_regions = otp_dev->ops->get_nr_regions(otp_dev);
	otp_dev_put_and_unlock(otp_dev);

	if (nr_regions < 0)
		return nr_regions;

	return sprintf(buf, "%zd\n", nr_regions);
}

static ssize_t otp_num_regions_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct otp_device *otp_dev = to_otp_device(dev);
	unsigned long nr_regions;
	ssize_t err = 0;

	if (!otp_dev->ops->set_nr_regions)
		return -EOPNOTSUPP;

	err = strict_strtoul(buf, 0, &nr_regions);
	if (err)
		return err;

	err = otp_dev_get_and_lock(otp_dev);
	if (err)
		return err;

	if (!otp_write_enabled(otp_dev)) {
		err = -EPERM;
		goto out;
	}

	err = otp_dev->ops->set_nr_regions(otp_dev, nr_regions);

out:
	otp_dev_put_and_unlock(otp_dev);

	return err ?: len;
}
static DEVICE_ATTR(num_regions, S_IRUSR | S_IWUSR, otp_num_regions_show,
		   otp_num_regions_store);

static ssize_t otp_word_size_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	return sprintf(buf, "%zu\n", otp_dev->word_sz);
}
static DEVICE_ATTR(word_size, S_IRUSR, otp_word_size_show, NULL);

static struct attribute *otp_attrs[] = {
	&dev_attr_strict_programming.attr,
	&dev_attr_num_regions.attr,
	&dev_attr_write_enable.attr,
	&dev_attr_word_size.attr,
	NULL,
};

static const struct attribute_group otp_attr_group = {
	.attrs		= otp_attrs,
};

static const struct attribute_group *otp_attr_groups[] = {
	&otp_attr_group,
	NULL,
};

static struct device_type otp_type = {
	.name		= "otp",
	.groups		= otp_attr_groups,
};

static void otp_dev_release(struct device *dev)
{
	struct otp_device *otp_dev = to_otp_device(dev);

	mutex_lock(&otp_register_mutex);
	clear_bit(otp_dev->dev_nr, registered_otp_map);
	mutex_unlock(&otp_register_mutex);
	unregister_chrdev_region(otp_dev->devno, otp_dev->max_regions);
	kfree(otp_dev);
}

struct otp_device *otp_device_alloc(struct device *dev,
				    const struct otp_device_ops *ops,
				    size_t size, size_t word_sz,
				    unsigned max_regions,
				    unsigned long flags)
{
	struct otp_device *otp_dev;
	int err = -EBUSY, otp_nr;

	mutex_lock(&otp_register_mutex);
	otp_nr = find_first_zero_bit(registered_otp_map, MAX_OTP_DEVICES);
	if (otp_nr < MAX_OTP_DEVICES)
		set_bit(otp_nr, registered_otp_map);
	mutex_unlock(&otp_register_mutex);

	if (otp_nr == MAX_OTP_DEVICES)
		goto out;

	if (word_sz != OTP_WORD_SIZE) {
		dev_err(dev, "otp word size of %zu is not supported\n",
			word_sz);
		err = -EINVAL;
		goto out_clear;
	}

	if (!dev || !get_device(dev)) {
		err = -ENODEV;
		goto out_clear;
	}

	otp_dev = kzalloc(sizeof(*otp_dev), GFP_KERNEL);
	if (!otp_dev) {
		err = -ENOMEM;
		goto out_put;
	}

	err = alloc_chrdev_region(&otp_dev->devno, 0, max_regions, "otp");
	if (err)
		goto out_put;

	INIT_LIST_HEAD(&otp_dev->regions);
	mutex_init(&otp_dev->lock);
	otp_dev->ops		= ops;
	otp_dev->dev.release	= otp_dev_release;
	otp_dev->dev.bus	= &otp_bus_type;
	otp_dev->dev.type	= &otp_type;
	otp_dev->dev.parent	= dev;
	otp_dev->size		= size;
	otp_dev->max_regions	= max_regions;
	otp_dev->dev_nr		= otp_nr;
	otp_dev->flags		= flags;
	otp_dev->word_sz	= word_sz;
	dev_set_name(&otp_dev->dev, "otp%c", 'a' + otp_dev->dev_nr);

	otp_dev = otp_dev;
	err = device_register(&otp_dev->dev);
	if (err) {
		dev_err(&otp_dev->dev, "couldn't add device\n");
		goto out_unalloc_chrdev;
	}
	pr_info("device %s of %zu bytes registered\n", ops->name, size);
	return otp_dev;

out_unalloc_chrdev:
	unregister_chrdev_region(otp_dev->devno, otp_dev->max_regions);
out_put:
	if (dev)
		put_device(dev);
out_clear:
	clear_bit(otp_nr, registered_otp_map);
out:
	return err ? ERR_PTR(err) : otp_dev;
}
EXPORT_SYMBOL_GPL(otp_device_alloc);

void otp_device_unregister(struct otp_device *dev)
{
	struct otp_region *region, *tmp;

	list_for_each_entry_safe(region, tmp, &dev->regions, head)
		otp_region_unregister(region);
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL_GPL(otp_device_unregister);

static void otp_region_release(struct device *dev)
{
	struct otp_region *region = to_otp_region(dev);

	kfree(region->label);
	cdev_del(&region->cdev);
	list_del(&region->head);
	kfree(region);
}

static int otp_open(struct inode *inode, struct file *filp)
{
	struct otp_region *region;
	struct otp_device *otp_dev;
	int ret = 0;

	region = container_of(inode->i_cdev, struct otp_region, cdev);
	otp_dev = to_otp_device(region->dev.parent);

	if (!try_module_get(otp_dev->ops->owner)) {
		ret = -ENODEV;
		goto out;
	}

	if (!get_device(&region->dev)) {
		ret = -ENODEV;
		goto out_put_module;
	}
	filp->private_data = region;

	goto out;

out_put_module:
	module_put(otp_dev->ops->owner);
out:
	return ret;
}

static int otp_release(struct inode *inode, struct file *filp)
{
	struct otp_region *region;
	struct otp_device *otp_dev;

	region = container_of(inode->i_cdev, struct otp_region, cdev);
	otp_dev = to_otp_device(region->dev.parent);

	region = filp->private_data;
	put_device(&region->dev);
	module_put(otp_dev->ops->owner);

	return 0;
}

#ifdef CONFIG_OTP_WRITE_ENABLE
/*
 * Write to the otp memory from a userspace buffer. This requires that the
 * write_enable attribute is set to "enabled" in
 * /sys/bus/otp/devices/otp#/write_enable
 *
 * If writing is not enabled, this should return -EPERM.
 *
 * The write method takes a buffer from userspace and writes it into the
 * corresponding bits of the otp. The current file offset refers to the byte
 * address of the words in the otp region that should be written to.
 * Therefore:
 *
 *	- we may have to do a read-modify-write to get up to an aligned
 *	boundary, then
 *	- do a series of word writes, followed by,
 *	- an optional final read-modify-write if there are less than
 *	OTP_WORD_SIZE bytes left to write.
 *
 * After writing, the file offset will be updated to the next byte address. If
 * one word fails to write then the writing is aborted at that point and no
 * further data is written. If the user can carry on then they may call
 * write(2) again with an updated offset.
 */
static ssize_t otp_write(struct file *filp, const char __user *buf, size_t len,
			 loff_t *offs)
{
	ssize_t ret = 0;
	u64 word;
	ssize_t written = 0;
	struct otp_region *region = filp->private_data;
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	unsigned pos = (unsigned)*offs;
	enum otp_redundancy_fmt fmt;

	if (mutex_lock_interruptible(&otp_dev->lock))
		return -ERESTARTSYS;

	if (region->ops->get_fmt)
		fmt = region->ops->get_fmt(region);
	else
		fmt = OTP_REDUNDANCY_FMT_SINGLE_ENDED;

	if (*offs >= region->ops->get_size(region)) {
		ret = -ENOSPC;
		goto out;
	}

	if (!otp_write_enabled(otp_dev)) {
		ret = -EPERM;
		goto out;
	}

	len = min_t(size_t, len, region->ops->get_size(region) - *offs);
	if (!len) {
		ret = 0;
		goto out;
	}

	if ((otp_dev->flags & OTP_CAPS_NO_SUBWORD_WRITE) &&
	    ((len & otp_dev->word_sz) || (pos & otp_dev->word_sz))) {
		dev_info(&otp_dev->dev, "unable to perform partial word writes\n");
		ret = -EMSGSIZE;
		goto out;
	}

	if (otp_dev->ops->set_fmt)
		otp_dev->ops->set_fmt(otp_dev, fmt);

	if (pos & (OTP_WORD_SIZE - 1)) {
		/*
		 * We're not currently on a otp word aligned boundary so we
		 * need to do a read-modify-write.
		 */
		unsigned word_addr = pos / OTP_WORD_SIZE;
		unsigned offset = pos % OTP_WORD_SIZE;
		size_t bytes = min_t(size_t, OTP_WORD_SIZE - offset, len);

		ret = otp_dev->ops->read_word(otp_dev, region, word_addr,
					      &word);
		if (ret)
			goto out;

		if (copy_from_user((void *)(&word) + offset, buf, bytes)) {
			ret = -EFAULT;
			goto out;
		}

		ret = otp_dev->ops->write_word(otp_dev, region, word_addr,
					       word);
		if (ret)
			goto out;

		written += bytes;
		len -= bytes;
		buf += bytes;
		pos += bytes;
	}

	/*
	 * We're now aligned to OTP word byte boundary so we can simply copy
	 * words from userspace and write them into the otp.
	 */
	while (len >= OTP_WORD_SIZE) {
		if (copy_from_user(&word, buf, OTP_WORD_SIZE)) {
			ret = -EFAULT;
			goto out;
		}

		ret = otp_dev->ops->write_word(otp_dev, region,
					       pos / OTP_WORD_SIZE, word);
		if (ret)
			goto out;

		written += OTP_WORD_SIZE;
		len -= OTP_WORD_SIZE;
		buf += OTP_WORD_SIZE;
		pos += OTP_WORD_SIZE;
	}

	/*
	 * We might have less than a full OTP word left so we'll need to do
	 * another read-modify-write.
	 */
	if (len) {
		ret = otp_dev->ops->read_word(otp_dev, region,
					      pos / OTP_WORD_SIZE, &word);
		if (ret)
			goto out;

		if (copy_from_user(&word, buf, len)) {
			ret = -EFAULT;
			goto out;
		}

		ret = otp_dev->ops->write_word(otp_dev, region,
					       pos / OTP_WORD_SIZE, word);
		if (ret)
			goto out;

		written += len;
		buf += len;
		pos += len;
		len = 0;
	}

	*offs += written;

out:
	mutex_unlock(&otp_dev->lock);
	return ret ?: written;
}
#else /* CONFIG_OTP_WRITE_ENABLE */
static ssize_t otp_write(struct file *filp, const char __user *buf, size_t len,
			 loff_t *offs)
{
	return -EACCES;
}
#endif /* CONFIG_OTP_WRITE_ENABLE */

/*
 * Lock an area of an OTP region down.  We can lock multiple words in a
 * request and we do this one word at a time.  It is not possible to lock a
 * sub-word area.
 */
static long otp_lock_area(struct otp_region *region, unsigned long arg)
{
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	struct otp_lock_area_info info;
	size_t n, words_locked = 0;
	unsigned long word_addr;
	long ret;
	ssize_t region_sz = region->ops->get_size(region);

	if (!otp_write_enabled(otp_dev))
		return -EPERM;

	if (!otp_dev->ops->lock_word)
		return -EOPNOTSUPP;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	if ((info.byte_addr & otp_dev->word_sz) ||
	    (info.byte_count & otp_dev->word_sz) ||
	    (info.byte_addr + info.byte_count > (size_t)region_sz))
		return -EMSGSIZE;

	for (n = 0, word_addr = info.byte_addr / otp_dev->word_sz;
	     n < info.byte_count / otp_dev->word_sz; ++n, ++word_addr) {
		ret = otp_dev->ops->lock_word(otp_dev, region, word_addr);
		if (ret) {
			dev_warn(&region->dev, "failed to lock word %lu\n",
				 word_addr);
			break;
		}
		++words_locked;
	}

	info.byte_count = words_locked * otp_dev->word_sz;
	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		ret = -EFAULT;

	return ret;
}

static long otp_ioctl(struct file *filp, unsigned cmd, unsigned long arg)
{
	struct otp_region *region = filp->private_data;
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	long ret = -ENOTTY;

	if (mutex_lock_interruptible(&otp_dev->lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case OTP_LOCK_AREA:
		ret = otp_lock_area(region, arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&otp_dev->lock);

	return ret;
}

/*
 * Read an otp region. This switches the otp into the appropriate redundancy
 * format so we can simply read from the beginning of the region and copy it
 * into the user buffer.
 */
static ssize_t otp_read(struct file *filp, char __user *buf, size_t len,
			loff_t *offs)
{
	ssize_t ret = 0;
	u64 word;
	ssize_t bytes_read = 0;
	struct otp_region *region = filp->private_data;
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	unsigned pos = (unsigned)*offs;
	enum otp_redundancy_fmt fmt;

	if (mutex_lock_interruptible(&otp_dev->lock))
		return -ERESTARTSYS;

	if (region->ops->get_fmt)
		fmt = region->ops->get_fmt(region);
	else
		fmt = OTP_REDUNDANCY_FMT_SINGLE_ENDED;

	if (*offs >= region->ops->get_size(region)) {
		ret = 0;
		goto out;
	}

	len = min(len, region->ops->get_size(region) - (unsigned)*offs);
	if (!len) {
		ret = 0;
		goto out;
	}

	if (otp_dev->ops->set_fmt)
		otp_dev->ops->set_fmt(otp_dev, fmt);

	if (pos & (OTP_WORD_SIZE - 1)) {
		/*
		 * We're not currently on an 8 byte boundary so we need to
		 * read to a bounce buffer then do the copy_to_user() with an
		 * offset.
		 */
		unsigned word_addr = pos / OTP_WORD_SIZE;
		unsigned offset = pos % OTP_WORD_SIZE;
		size_t bytes = min_t(size_t, OTP_WORD_SIZE - offset, len);

		ret = otp_dev->ops->read_word(otp_dev, region, word_addr,
					      &word);
		if (ret)
			goto out;

		if (copy_to_user(buf, (void *)(&word) + offset, bytes)) {
			ret = -EFAULT;
			goto out;
		}

		bytes_read += bytes;
		len -= bytes;
		buf += bytes;
		pos += bytes;
	}

	/*
	 * We're now aligned to an 8 byte boundary so we can simply copy words
	 * from the bounce buffer directly with a copy_to_user().
	 */
	while (len >= OTP_WORD_SIZE) {
		ret = otp_dev->ops->read_word(otp_dev, region,
					      pos / OTP_WORD_SIZE, &word);
		if (ret)
			goto out;

		if (copy_to_user(buf, &word, OTP_WORD_SIZE)) {
			ret = -EFAULT;
			goto out;
		}

		bytes_read += OTP_WORD_SIZE;
		len -= OTP_WORD_SIZE;
		buf += OTP_WORD_SIZE;
		pos += OTP_WORD_SIZE;
	}

	/*
	 * We might have less than 8 bytes left so we'll need to do another
	 * copy_to_user() but with a partial word length.
	 */
	if (len) {
		ret = otp_dev->ops->read_word(otp_dev, region,
					      pos / OTP_WORD_SIZE, &word);
		if (ret)
			goto out;

		if (copy_to_user(buf, &word, len)) {
			ret = -EFAULT;
			goto out;
		}

		bytes_read += len;
		buf += len;
		pos += len;
		len = 0;
	}

	*offs += bytes_read;

out:
	mutex_unlock(&otp_dev->lock);
	return ret ?: bytes_read;
}

/*
 * Seek to a specified position in the otp region. This can be used if
 * userspace doesn't have pread()/pwrite() and needs to write to a specified
 * offset in the otp.
 */
static loff_t otp_llseek(struct file *filp, loff_t offs, int origin)
{
	struct otp_region *region = filp->private_data;
	struct otp_device *otp_dev = to_otp_device(region->dev.parent);
	int ret = 0;
	loff_t end;

	if (mutex_lock_interruptible(&otp_dev->lock))
		return -ERESTARTSYS;

	switch (origin) {
	case SEEK_CUR:
		if (filp->f_pos + offs < 0 ||
		    filp->f_pos + offs >= region->ops->get_size(region))
			ret = -EINVAL;
		else
			filp->f_pos += offs;
		break;

	case SEEK_SET:
		if (offs < 0 || offs >= region->ops->get_size(region))
			ret = -EINVAL;
		else
			filp->f_pos = offs;
		break;

	case SEEK_END:
		end = region->ops->get_size(region) - 1;
		if (end + offs < 0 || end + offs >= end)
			ret = -EINVAL;
		else
			filp->f_pos = end + offs;
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&otp_dev->lock);

	return ret ?: filp->f_pos;
}

static const struct file_operations otp_fops = {
	.owner		= THIS_MODULE,
	.open		= otp_open,
	.release	= otp_release,
	.write		= otp_write,
	.read		= otp_read,
	.llseek		= otp_llseek,
	.unlocked_ioctl	= otp_ioctl,
	.compat_ioctl	= otp_ioctl,
};

struct otp_region *__otp_region_alloc(struct otp_device *dev,
				      const struct otp_region_ops *ops,
				      int region_nr, const char *fmt,
				      va_list vargs)
{
	struct otp_region *region;
	int err = 0;
	dev_t devno = MKDEV(MAJOR(dev->devno), region_nr + 1);

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		err = -ENOMEM;
		goto out;
	}

	region->label		= kvasprintf(GFP_KERNEL, fmt, vargs);
	if (!region->label)
		goto out_free;

	region->ops		= ops;
	region->region_nr	= region_nr;
	region->dev.parent	= &dev->dev;
	region->dev.release	= otp_region_release;
	region->dev.devt	= devno;
	region->dev.type	= &region_type;
	dev_set_name(&region->dev, "otpa%d", region_nr + 1);

	cdev_init(&region->cdev, &otp_fops);
	err = cdev_add(&region->cdev, devno, 1);
	if (err) {
		dev_err(&region->dev, "couldn't create cdev\n");
		goto out_free_name;
	}

	err = device_register(&region->dev);
	if (err) {
		dev_err(&region->dev, "couldn't add device\n");
		goto out_cdev_del;
	}

	list_add_tail(&region->head, &dev->regions);
	goto out;

out_cdev_del:
	cdev_del(&region->cdev);
out_free_name:
	kfree(region->label);
out_free:
	kfree(region);
out:
	return err ? ERR_PTR(err) : region;
}

struct otp_region *otp_region_alloc_unlocked(struct otp_device *dev,
					     const struct otp_region_ops *ops,
					     int region_nr, const char *fmt,
					     ...)
{
	struct otp_region *ret;
	va_list vargs;

	va_start(vargs, fmt);
	ret = __otp_region_alloc(dev, ops, region_nr, fmt, vargs);
	va_end(vargs);

	return ret;
}
EXPORT_SYMBOL_GPL(otp_region_alloc_unlocked);

struct otp_region *otp_region_alloc(struct otp_device *dev,
				    const struct otp_region_ops *ops,
				    int region_nr, const char *fmt, ...)
{
	struct otp_region *ret;
	va_list vargs;

	mutex_lock(&dev->lock);
	va_start(vargs, fmt);
	ret = __otp_region_alloc(dev, ops, region_nr, fmt, vargs);
	va_end(vargs);
	mutex_unlock(&dev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(otp_region_alloc);

static int __init otp_init(void)
{
	return bus_register(&otp_bus_type);
}
module_init(otp_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("OTP bus driver");

/*
 * Copyright (c) 2006-2011 picoChip Designs Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This file defines the ioctl() calls used for picogpio configuration and any
 * associated shared structures that are used to pass data from userspace into
 * kernelspace.
 */
#ifndef __GPIO_IOCTL_H__
#define __GPIO_IOCTL_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#endif /* __KERNEL__ */

/*
 * Data structure used for passing GPIO commands from userspace to
 * kernel space.
 */
typedef struct {
	unsigned pin;		/* Pin emumeration to use */
	int value;		/* Value to read / write */
	int def_value;		/* Optional default value for output pins. */
} picogpio_op_t;

/*
 * Data structure used for passing SDGPIO commands from userspace to
 * kernel space.
 */
typedef struct {
	unsigned pin;		/* Pin emumeration to use */
	__u8 converter_size;	/* The converter size in bits */
	__u16 analogue_rate;	/* The analogue rate value of the DAC */
} picogpio_analogue_config_t;

/*
 * Enumeration for specifing pin direction
 */
enum picogpio_pin_direction {
	PICOGPIO_INPUT,		/* Pin is an input */
	PICOGPIO_OUTPUT,	/* Pin is an output */
};

#define PICOGPIO_IOCTL_BASE   'g'

#define PICOGPIO_IOCTL_START  (0x00)

#define PICOGPIO_ACQUIRE	_IOR(PICOGPIO_IOCTL_BASE, \
				     PICOGPIO_IOCTL_START + 0, picogpio_op_t)
#define PICOGPIO_RELEASE        _IOR(PICOGPIO_IOCTL_BASE, \
				     PICOGPIO_IOCTL_START + 1, picogpio_op_t)
#define PICOGPIO_GET_DIRECTION	_IOWR(PICOGPIO_IOCTL_BASE, \
				      PICOGPIO_IOCTL_START + 2, picogpio_op_t)

#define PICOGPIO_SET_DIRECTION	_IOR(PICOGPIO_IOCTL_BASE, \
				     PICOGPIO_IOCTL_START + 3, picogpio_op_t)
#define PICOGPIO_GET_VALUE	_IOWR(PICOGPIO_IOCTL_BASE, \
				      PICOGPIO_IOCTL_START + 4, picogpio_op_t)
#define PICOGPIO_SET_VALUE	_IOR(PICOGPIO_IOCTL_BASE, \
				     PICOGPIO_IOCTL_START + 5, picogpio_op_t)
#define PICOGPIO_ANALOGUE_CONFIG	_IOR(PICOGPIO_IOCTL_BASE, \
					     PICOGPIO_IOCTL_START + 6, \
					     picogpio_analogue_config_t)
#define PICOGPIO_IOCTL_NUM_IOCTL  7

#endif /* !__GPIO_IOCTL_H__ */

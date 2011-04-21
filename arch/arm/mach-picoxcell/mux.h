/*
 * linux/arch/arm/mach-picoxcell/mux.h
 *
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#ifndef __MUX_H__
#define __MUX_H__

#include <linux/list.h>
#include <linux/sysdev.h>

#include "soc.h"

enum mux_setting {
	MUX_PERIPHERAL_RSVD	= -1,
	MUX_UNMUXED,
	MUX_ARM,
	MUX_SD,
	MUX_PERIPHERAL_FRACN,
	MUX_PERIPHERAL_EBI,
	MUX_PERIPHERAL_PAI,
	MUX_PERIPHERAL_DECODE,
	MUX_PERIPHERAL_SSI,
	MUX_PERIPHERAL_MII,
	MUX_PERIPHERAL_MAXIM,
	NR_MUX_SETTINGS,
};

enum mux_flags {
	MUX_RO			= (1 << 0),
	MUX_INVERT_PERIPH	= (1 << 1),
	MUX_CONFIG_BUS		= (1 << 2),
};

extern ssize_t pin_show(struct sys_device *dev, struct sysdev_attribute *attr,
			char *buf);
extern ssize_t pin_store(struct sys_device *dev, struct sysdev_attribute *attr,
			 const char *buf, size_t len);

struct mux_def {
	struct sysdev_attribute	attr;
	const char		*name;
	int			armgpio;
	int			sdgpio;
	int			periph;
	s16			gpio_reg_offs;
	s16			gpio_reg_bit;
	s16			periph_reg;
	s16			periph_bit;
	u16			caeid;
	u16			caddr;
	u16			mask;
	unsigned		flags;
	struct list_head	head;
};

struct mux_cfg {
	const char		*name;
	enum mux_setting	setting;
};

extern int mux_configure_one(const char *name, enum mux_setting setting);
extern int mux_configure_table(const struct mux_cfg *cfg,
			       unsigned int nr_cfgs);

#define MUXGPIO(__name, __arm, __sd, __periph, __gpio_reg, __gpio_bit, \
		__periph_reg, __periph_bit, __flags) { \
	.name		= #__name, \
	.armgpio	= __arm, \
	.sdgpio		= __sd, \
	.periph		= MUX_PERIPHERAL_ ## __periph, \
	.gpio_reg_offs	= __gpio_reg, \
	.gpio_reg_bit	= __gpio_bit, \
	.periph_reg	= __periph_reg, \
	.periph_bit	= __periph_bit, \
	.flags		= __flags, \
	.attr		= _SYSDEV_ATTR(__name, 0644, pin_show, pin_store), \
}

#define MUXCFGBUS(__name, __arm, __sd, __periph, __caeid, __caddr, __mask) { \
	.name		= #__name, \
	.armgpio	= __arm, \
	.sdgpio		= __sd, \
	.periph		= MUX_PERIPHERAL_ ## __periph, \
	.caeid		= __caeid, \
	.caddr		= __caddr, \
	.flags		= MUX_CONFIG_BUS, \
	.mask		= __mask, \
	.attr		= _SYSDEV_ATTR(__name, 0644, pin_show, pin_store), \
}

extern void picoxcell_mux_register(struct mux_def *defs, int nr_defs);

#endif /* __MUX_H__ */

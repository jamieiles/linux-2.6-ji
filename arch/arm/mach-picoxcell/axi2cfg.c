/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This file implements functions for using the axi2cfg to configure and debug
 * picoArray systems providing configuration bus access over the axi2cfg.
 */
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include <mach/hardware.h>

static void __iomem *axi2cfg;

unsigned long axi2cfg_readl(unsigned long offs)
{
	return readl(axi2cfg + offs);
}

void axi2cfg_writel(unsigned long val, unsigned long offs)
{
	writel(val, axi2cfg + offs);
}
EXPORT_SYMBOL_GPL(axi2cfg_writel);

void __init axi2cfg_init(void)
{
	axi2cfg = ioremap(PICOXCELL_AXI2CFG_BASE, 0x300);
	BUG_ON(!axi2cfg);
}
EXPORT_SYMBOL_GPL(axi2cfg_readl);

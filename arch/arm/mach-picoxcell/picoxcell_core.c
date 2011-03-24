/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/hardware/vic.h>
#include <asm/mach-types.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

struct dentry *picoxcell_debugfs;

struct picoxcell_soc *picoxcell_get_soc(void)
{
	unsigned long device_id =
		__raw_readl(IO_ADDRESS(PICOXCELL_AXI2CFG_BASE +
				       AXI2CFG_DEVICE_ID_REG_OFFSET));
	switch (device_id) {
	case 0x8003:
	case 0x8007:
		return &pc3x2_soc;

	case 0x20:
	case 0x21:
	case 0x22:
		return &pc3x3_soc;

	default:
		panic("unsupported device type %lx", device_id);
	}
}

void __init picoxcell_init_irq(void)
{
	u32 vic0_resume_sources =
		(1 << (IRQ_AXI2PICO8 & 31)) |
		(1 << (IRQ_EMAC & 31)) |
		(1 << (IRQ_WDG & 31));

	vic_init(IO_ADDRESS(PICOXCELL_VIC0_BASE), 32, 0xFFFFFFFE,
		 vic0_resume_sources);
	vic_init(IO_ADDRESS(PICOXCELL_VIC1_BASE), 0, 0x7FF, 0);
}

static const char *picoxcell_get_partname(void)
{
	unsigned long dev_id = axi2cfg_readl(AXI2CFG_DEVICE_ID_REG_OFFSET);
	const char *part = "<unknown>";

	if (dev_id == 0x8003)
		part = "pc302";
	else if (dev_id == 0x8007)
		part = "pc312";
	else if (dev_id == 0x20)
		part = "pc313";
	else if (dev_id == 0x21)
		part = "pc323";
	else if (dev_id == 0x22)
		part = "pc333";

	return part;
}

static inline unsigned long picoxcell_get_revision(void)
{
	return axi2cfg_readl(AXI2CFG_REVISION_ID_REG_OFFSET);
}

static void report_chipinfo(void)
{
	const char *part = picoxcell_get_partname();
	unsigned long revision = picoxcell_get_revision();

	pr_info("Picochip picoXcell device: %s revision %lu\n", part, revision);
}

static void picoxcell_debugfs_init(void)
{
	picoxcell_debugfs = debugfs_create_dir("picoxcell", NULL);

	if (IS_ERR(picoxcell_debugfs) &&
	    -ENODEV != PTR_ERR(picoxcell_debugfs)) {
		pr_err("failed to create picoxcell debugfs entry (%ld)\n",
		       PTR_ERR(picoxcell_debugfs));
		picoxcell_debugfs = NULL;
	}
}

void __init picoxcell_init_early(void)
{
	struct picoxcell_soc *soc = picoxcell_get_soc();

	axi2cfg_init();
	picoxcell_sched_clock_init();
	soc->init_clocks();
}

void __init picoxcell_core_init(void)
{
	struct picoxcell_soc *soc = picoxcell_get_soc();

	report_chipinfo();
	picoxcell_debugfs_init();

	soc->init();
}

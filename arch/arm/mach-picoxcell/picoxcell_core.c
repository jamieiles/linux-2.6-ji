/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/hardware/vic.h>
#include <asm/mach-types.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

static const struct picoxcell_timer picoxcell_timers[] = {
	{
		.name	= "timer0",
		.type	= TIMER_TYPE_TIMER,
		.base	= PICOXCELL_TIMER_BASE + 0 * TIMER_SPACING,
		.irq	= IRQ_TIMER0,
	},
	{
		.name	= "timer1",
		.type	= TIMER_TYPE_TIMER,
		.base	= PICOXCELL_TIMER_BASE + 1 * TIMER_SPACING,
		.irq	= IRQ_TIMER1,
	},
	{
		.name	= "rtc",
		.type	= TIMER_TYPE_RTC,
		.base	= PICOXCELL_RTCLK_BASE,
		.irq	= IRQ_RTC,
	},
};

static struct picoxcell_soc generic_soc = {
	.timers		= picoxcell_timers,
	.nr_timers	= ARRAY_SIZE(picoxcell_timers),
};

struct picoxcell_soc *picoxcell_get_soc(void)
{
	return &generic_soc;
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

void __init picoxcell_init_early(void)
{
	axi2cfg_init();
}

void __init picoxcell_core_init(void)
{
	report_chipinfo();
}

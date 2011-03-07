/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

FIXED_CLK(dummy,	CLOCK_TICK_RATE, -1);

static struct clk_lookup pc3x2_clk_lookup[] = {
	CLK_LOOKUP("dw_spi_mmio.0",	NULL,		&dummy_clk),
	CLK_LOOKUP("dw_dmac.0",		NULL,		&dummy_clk),
	CLK_LOOKUP("dw_dmac.1",		NULL,		&dummy_clk),
	CLK_LOOKUP(NULL,		"ebi",		&dummy_clk),
	CLK_LOOKUP(NULL,		"tzprot_ctrl",	&dummy_clk),
	CLK_LOOKUP("picoxcell-ipsec",	NULL,		&dummy_clk),
	CLK_LOOKUP("picoxcell-l2",	NULL,		&dummy_clk),
	CLK_LOOKUP("picoxcell-fuse",	NULL,		&dummy_clk),
	CLK_LOOKUP("dw_wdt",		NULL,		&dummy_clk),
	CLK_LOOKUP("macb",		"pclk",		&dummy_clk),
	CLK_LOOKUP("macb",		"hclk",		&dummy_clk),
};

static void pc3x2_clk_init(void)
{
	picoxcell_clk_add(&dummy_clk);
	clkdev_add_table(pc3x2_clk_lookup, ARRAY_SIZE(pc3x2_clk_lookup));
}

static const struct picoxcell_timer pc3x2_timers[] = {
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
		.name	= "timer2",
		.type	= TIMER_TYPE_TIMER,
		.base	= PICOXCELL_TIMER_BASE + 2 * TIMER_SPACING,
		.irq	= IRQ_TIMER2,
	},
	{
		.name	= "timer3",
		.type	= TIMER_TYPE_TIMER,
		.base	= PICOXCELL_TIMER_BASE + 3 * TIMER_SPACING,
		.irq	= IRQ_TIMER3,
	},
	{
		.name	= "rtc",
		.type	= TIMER_TYPE_RTC,
		.base	= PICOXCELL_RTCLK_BASE,
		.irq	= IRQ_RTC,
	},
};

static void pc3x2_init(void);

struct picoxcell_soc pc3x2_soc = {
	.init		= pc3x2_init,
	.init_clocks	= pc3x2_clk_init,
	.timers		= pc3x2_timers,
	.nr_timers	= ARRAY_SIZE(pc3x2_timers),
};

static void pc3x2_init(void)
{
}

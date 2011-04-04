/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "mux.h"
#include "picoxcell_core.h"
#include "soc.h"

FIXED_CLK(dummy,	CLOCK_TICK_RATE, -1, NULL);

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

static struct mux_def pc3x2_mux[] = {
	/*	Name		ARM	SD	PERIPH	CAEID	CADDR	MASK*/
	MUXCFGBUS(sdgpio4,	-1,	4,	PAI,	0x8080,	0x9,	0x7),
	MUXCFGBUS(sdgpio5,	-1,	5,	PAI,	0x8080,	0x9,	0x6),
	MUXCFGBUS(sdgpio6,	-1,	6,	PAI,	0x8080,	0x9,	0x5),
	MUXCFGBUS(sdgpio7,	-1,	7,	PAI,	0x8080,	0x9,	0x4),

	MUXCFGBUS(arm4,		4,	-1,	PAI,	0x8080,	0x9,	0xb),
	MUXCFGBUS(arm5,		5,	-1,	PAI,	0x8080,	0x9,	0xa),
	MUXCFGBUS(arm6,		6,	-1,	PAI,	0x8080,	0x9,	0x9),
	MUXCFGBUS(arm7,		7,	-1,	PAI,	0x8080,	0x9,	0x8),

	/*	Name		ARM	SD	PERIPH	REG	BIT	PERREG	PERBIT	FLAGS */
	MUXGPIO(shared0,	8,	8,	FRACN,	0,	16,	0,	7,	0),
	MUXGPIO(shared1,	9,	9,	RSVD,	0,	17,	-1,	-1,	0),
	MUXGPIO(shared2,	10,	10,	RSVD,	0,	18,	-1,	-1,	0),
	MUXGPIO(shared3,	11,	11,	RSVD,	0,	19,	-1,	-1,	0),
	MUXGPIO(shared4,	12,	12,	RSVD,	0,	20,	-1,	-1,	0),
	MUXGPIO(shared5,	13,	13,	RSVD,	0,	21,	-1,	-1,	0),
	MUXGPIO(shared6,	14,	14,	RSVD,	0,	22,	-1,	-1,	0),
	MUXGPIO(shared7,	15,	15,	RSVD,	0,	23,	-1,	-1,	0),

	MUXGPIO(sdgpio0,	-1,	0,	FRACN,	-1,	-1,	0,	7,	MUX_INVERT_PERIPH),
};

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

static void pc3x2_add_gpio(void)
{
	picoxcell_add_gpio_port(0, 8, PC3X2_GPIO_PIN_ARM_0);
	picoxcell_add_gpio_port(1, 8, PC3X2_GPIO_PIN_ARM_8);
}

static void pc3x2_init_bus_snoopers(void)
{
	static const char *pc3x2_snoop_err_names[32] = {
		[0]	= "dmac1_channel0 (read)",
		[1]	= "dmac1_channel1 (read)",
		[2]	= "dmac1_channel2 (read)",
		[3]	= "dmac1_channel3 (read)",
		[4]	= "dmac2_channel0 (read)",
		[5]	= "dmac2_channel1 (read)",
		[6]	= "dmac2_channel2 (read)",
		[7]	= "dmac2_channel3 (read)",
		[8]	= "emac (read)",
		[9]	= "cipher (read)",
		[10]	= "srtp (read)",
		[11]	= "ipsec (read)",
		[12]	= "dmac1_channel0 (write)",
		[13]	= "dmac1_channel1 (write)",
		[14]	= "dmac1_channel2 (write)",
		[15]	= "dmac1_channel3 (write)",
		[16]	= "dmac2_channel0 (write)",
		[17]	= "dmac2_channel1 (write)",
		[18]	= "dmac2_channel2 (write)",
		[19]	= "dmac2_channel3 (write)",
		[20]	= "emac (write)",
		[21]	= "cipher (write)",
		[22]	= "srtp (write)",
		[23]	= "ipsec (write)",
	};

	static struct resource irqs[] = {
		{
			.start	= IRQ_AXI_RD_ERR,
			.end	= IRQ_AXI_RD_ERR,
			.flags	= IORESOURCE_IRQ,
		},
		{
			.start	= IRQ_AXI_WR_ERR,
			.end	= IRQ_AXI_WR_ERR,
			.flags	= IORESOURCE_IRQ,
		},
	};

	platform_device_register_resndata(NULL, "picoxcell-bus-error", -1,
					  irqs, ARRAY_SIZE(irqs),
					  pc3x2_snoop_err_names,
					  sizeof(pc3x2_snoop_err_names));
}

static void pc3x2_init(void)
{
	picoxcell_mux_register(pc3x2_mux, ARRAY_SIZE(pc3x2_mux));
	pc3x2_add_gpio();
	pc3x2_init_bus_snoopers();
}

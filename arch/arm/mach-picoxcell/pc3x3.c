/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

static DEFINE_SPINLOCK(pc3x3_clk_lock);

FIXED_CLK(tzprot,	CLOCK_TICK_RATE, 0);
FIXED_CLK(spi,		CLOCK_TICK_RATE, 1);
FIXED_CLK(dmac0,	CLOCK_TICK_RATE, 2);
FIXED_CLK(dmac1,	CLOCK_TICK_RATE, 3);
FIXED_CLK(ebi,		CLOCK_TICK_RATE, 4);
FIXED_CLK(ipsec,	CLOCK_TICK_RATE, 5);
FIXED_CLK(l2_engine,	CLOCK_TICK_RATE, 6);
FIXED_CLK(trng,		CLOCK_TICK_RATE, 7);
FIXED_CLK(fuse,		CLOCK_TICK_RATE, 8);
FIXED_CLK(otp,		CLOCK_TICK_RATE, 9);
FIXED_CLK(wdt,		CLOCK_TICK_RATE, -1);
FIXED_CLK(dummy,	CLOCK_TICK_RATE, -1);
VARIABLE_CLK(arm,			 -1, 140000, 700000, 5000);

static struct clk *pc3x3_clks[] = {
	&tzprot_clk,
	&spi_clk,
	&dmac0_clk,
	&dmac1_clk,
	&ebi_clk,
	&ipsec_clk,
	&l2_engine_clk,
	&trng_clk,
	&fuse_clk,
	&otp_clk,
	&wdt_clk,
	&arm_clk,
};

static struct clk_lookup pc3x3_clk_lookup[] = {
	CLK_LOOKUP(NULL,		"tzprot_ctl",	&tzprot_clk),
	CLK_LOOKUP("dw_spi_mmio.0",	NULL,		&spi_clk),
	CLK_LOOKUP("dw_dmac.0",		NULL,		&dmac0_clk),
	CLK_LOOKUP("dw_dmac.1",		NULL,		&dmac1_clk),
	CLK_LOOKUP(NULL,		"ebi",		&ebi_clk),
	CLK_LOOKUP("picoxcell-ipsec",	NULL,		&ipsec_clk),
	CLK_LOOKUP("picoxcell-l2",	NULL,		&l2_engine_clk),
	CLK_LOOKUP("picoxcell-trng",	NULL,		&trng_clk),
	CLK_LOOKUP("picoxcell-fuse",	NULL,		&fuse_clk),
	CLK_LOOKUP("picoxcell-otp",	NULL,		&otp_clk),
	CLK_LOOKUP("dw_wdt",		NULL,		&wdt_clk),
	CLK_LOOKUP(NULL,		"arm",		&arm_clk),
	CLK_LOOKUP("macb",		"pclk",		&dummy_clk),
	CLK_LOOKUP("macb",		"hclk",		&dummy_clk),
};

static int pc3x3_clk_is_enabled(struct clk *clk)
{
	unsigned long clk_gate =
		axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	return !(clk_gate & (1 << clk->clk_num));
}

static void pc3x3_clk_disable(struct clk *clk)
{
	unsigned long clk_gate;

	if (clk->clk_num < 0)
		return;

	/*
	 * Make sure that all outstanding transactions have reached the device
	 * before we turn off the clock to prevent taking an exception.
	 */
	dsb();

	clk_gate = axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	clk_gate |= (1 << clk->clk_num);
	axi2cfg_writel(clk_gate, AXI2CFG_CLOCK_GATING_REG_OFFSET);
}

static inline void pc3x3_clk_enable(struct clk *clk)
{
	unsigned long clk_gate;

	if (clk->clk_num < 0)
		return;

	clk_gate = axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	clk_gate &= ~(1 << clk->clk_num);
	axi2cfg_writel(clk_gate, AXI2CFG_CLOCK_GATING_REG_OFFSET);
}

static long pc3x3_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret = -EINVAL;
	unsigned long offset = rate % clk->step;

	if (WARN_ON(clk != &arm_clk))
		goto out;

	rate -= offset;
	if (offset > clk->step - offset)
		ret = rate + clk->step;
	else
		ret = rate;

out:
	return ret;
}

/* The register that the CLKF value is programmed into. */
#define AXI2CFG_ARM_PLL_CLKF_REG_OFFS		0x0050
/* The frequency sensing control register. */
#define AXI2CFG_ARM_PLL_FREQ_SENSE_REG_OFFS	0x0054

/* The value in the sense register is a valid frequency. */
#define AXI2CFG_ARM_PLL_FREQ_SENSE_VALID	(1 << 29)
/* The sensing process is active. */
#define AXI2CFG_ARM_PLL_FREQ_SENSE_ACTIVE	(1 << 30)
/* Write this to the sense register to start sensing. Self clearing. */
#define AXI2CFG_ARM_PLL_FREQ_SENSE_START	(1 << 31)
/*
 * The frequency (in MHz) is returned in the bottom 10 bits of the sense
 * register and is valid when bit 29 is asserted.
 */
#define AXI2CFG_ARM_PLL_FREQ_SENSE_FREQ_MASK	0x3FF

static int __pc3x3_clk_get_rate(struct clk *clk)
{
	unsigned int mhz = 0;
	unsigned long sense_val;
	int ret = -EINVAL;

	if (WARN_ON(clk != &arm_clk))
		goto out;

	while (0 == mhz) {
		do {
			axi2cfg_writel(AXI2CFG_ARM_PLL_FREQ_SENSE_START,
				       AXI2CFG_ARM_PLL_FREQ_SENSE_REG_OFFS);

			/* Wait for the frequency sense to complete. */
			do {
				sense_val = axi2cfg_readl(AXI2CFG_ARM_PLL_FREQ_SENSE_REG_OFFS);
			} while ((sense_val &
				  AXI2CFG_ARM_PLL_FREQ_SENSE_ACTIVE));
		} while (!(sense_val & AXI2CFG_ARM_PLL_FREQ_SENSE_VALID));

		/* The frequency sense returns the frequency in MHz. */
		mhz = (sense_val & AXI2CFG_ARM_PLL_FREQ_SENSE_FREQ_MASK);
	}
	ret = mhz * 1000000;

out:
	return ret;
}

static int pc3x3_clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pc3x3_clk_lock, flags);
	ret = __pc3x3_clk_get_rate(clk);
	spin_unlock_irqrestore(&pc3x3_clk_lock, flags);

	return ret;
}

static void
pc3x3_cpu_pll_set(unsigned int freq)
{
	/* Set the new frequency. */
	axi2cfg_writel(((freq / 1000) / 5) - 1, AXI2CFG_ARM_PLL_CLKF_REG_OFFS);
	udelay(2);
}

static int pc3x3_clk_set_rate(struct clk *clk, unsigned long target)
{
	int ret = -EINVAL;
	unsigned long flags, current_khz;

	if (WARN_ON(clk != &arm_clk) || target % clk->step) {
		pr_err("unable to set rate for non-cpu clock (%lu)\n", target);
		goto out;
	}

	target /= 1000;
	pr_debug("set cpu clock rate to %luKHz\n", target);

	spin_lock_irqsave(&pc3x3_clk_lock, flags);

	/*
	 * We can only reliably step by 20% at a time. We may need to
	 * do this in several iterations.
	 */
	while ((current_khz = __pc3x3_clk_get_rate(clk) / 1000) != target) {
		unsigned long next_step, next_target;

		if (target < current_khz) {
			next_step = current_khz - ((4 * current_khz) / 5);
			next_target = current_khz -
				min(current_khz - target, next_step);
			next_target = roundup(next_target, clk->step);
		} else {
			next_step = ((6 * current_khz) / 5) - current_khz;
			next_target =
				min(target - current_khz, next_step) +
				current_khz;
			next_target =
				(next_target / clk->step) * clk->step;
		}

		pc3x3_cpu_pll_set(next_target);
	}

	spin_unlock_irqrestore(&pc3x3_clk_lock, flags);
	ret = 0;

out:
	return ret;
}

static void pc3x3_clk_init(void)
{
	int i;

	clkdev_add_table(pc3x3_clk_lookup, ARRAY_SIZE(pc3x3_clk_lookup));

	for (i = 0; i < ARRAY_SIZE(pc3x3_clks); ++i) {
		struct clk *clk = pc3x3_clks[i];

		clk->enable	= pc3x3_clk_enable;
		clk->disable	= pc3x3_clk_disable;
		clk->is_enabled	= pc3x3_clk_is_enabled;

		if (clk->rate < 0) {
			clk->round_rate	= pc3x3_clk_round_rate;
			clk->set_rate	= pc3x3_clk_set_rate;
			clk->get_rate	= pc3x3_clk_get_rate;
		}

		picoxcell_clk_add(clk);
	}

	/*
	 * For PC3x3, disable the clocks that aren't required in the core
	 * code. The drivers will enable the clocks when they get initialised.
	 */
	__clk_disable(&spi_clk);
	__clk_disable(&dmac0_clk);
	__clk_disable(&dmac1_clk);
	__clk_disable(&ipsec_clk);
	__clk_disable(&l2_engine_clk);
	__clk_disable(&trng_clk);
	__clk_disable(&otp_clk);
}

static const struct picoxcell_timer pc3x3_timers[] = {
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
		.base	= PC3X3_TIMER2_BASE + 0 * TIMER_SPACING,
		.irq	= IRQ_TIMER2,
	},
	{
		.name	= "timer3",
		.type	= TIMER_TYPE_TIMER,
		.base	= PC3X3_TIMER2_BASE + 1 * TIMER_SPACING,
		.irq	= IRQ_TIMER3,
	},
	{
		.name	= "rtc",
		.type	= TIMER_TYPE_RTC,
		.base	= PICOXCELL_RTCLK_BASE,
		.irq	= IRQ_RTC,
	},
};

static void pc3x3_init(void);

struct picoxcell_soc pc3x3_soc = {
	.init		= pc3x3_init,
	.init_clocks	= pc3x3_clk_init,
	.timers		= pc3x3_timers,
	.nr_timers	= ARRAY_SIZE(pc3x3_timers),
};

static void pc3x3_init(void)
{
}

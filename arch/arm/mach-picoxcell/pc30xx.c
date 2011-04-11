/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

#define AXI2CFG_INPUT_XTAL_CLOCK_REG_OFFSET		0x006C

/*
 * PLL mangagement registers.  These are offsets from .set_base in pc30xx_pll
 * which is in turn an offset from the AXI2CFG base.
 */
#define PLL_DIVF_REG_OFFSET				0x0
#define PLL_DIVQ_REG_OFFSET				0x4
#define PLL_CHANGE_REG_OFFSET				0x8

/*
 * PLL_CHANGE bit offsets.
 */
#define PLL_CHANGE_ACTIVE_MASK				(1 << 30)
#define PLL_CHANGE_START_MASK				(1 << 31)

/*
 * PLL sense register bits.
 */
#define PLL_SENSE_FREQ_MASK				0x7FF
#define PLL_SENSE_FREQ_VALID_MASK			(1 << 29)
#define PLL_SENSE_ACTIVE_MASK				(1 << 30)
#define PLL_SENSE_START_MASK				(1 << 31)

struct pc30xx_pll {
	struct clk		clk;
	unsigned long		set_base;
	unsigned long		sense_reg;
};

static inline struct pc30xx_pll *to_pc30xx_pll(struct clk *clk)
{
	return container_of(clk, struct pc30xx_pll, clk);
}

#define PC30XX_PLL(__name, __min, __max, __set_base, __sense_reg)	\
	static struct pc30xx_pll __name ## _clk = {			\
		.clk = VARIABLE_CLK_INIT(__name, -1, __min, __max, 1000,\
					 &pc30xx_variable_clk_ops),	\
		.set_base = __set_base,					\
		.sense_reg = __sense_reg,				\
	}

static DEFINE_SPINLOCK(pc30xx_clk_lock);

static int pc30xx_clk_is_enabled(struct clk *clk)
{
	unsigned long clk_gate =
		axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	return !(clk_gate & (1 << clk->clk_num));
}

static void pc30xx_clk_disable(struct clk *clk)
{
	unsigned long clk_gate, flags;

	if (clk->clk_num < 0)
		return;

	/*
	 * Make sure that all outstanding transactions have reached the device
	 * before we turn off the clock to prevent taking an exception.
	 */
	dsb();

	spin_lock_irqsave(&pc30xx_clk_lock, flags);
	clk_gate = axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	clk_gate |= (1 << clk->clk_num);
	axi2cfg_writel(clk_gate, AXI2CFG_CLOCK_GATING_REG_OFFSET);
	spin_unlock_irqrestore(&pc30xx_clk_lock, flags);
}

static inline void pc30xx_clk_enable(struct clk *clk)
{
	unsigned long clk_gate, flags;

	if (clk->clk_num < 0)
		return;

	spin_lock_irqsave(&pc30xx_clk_lock, flags);
	clk_gate = axi2cfg_readl(AXI2CFG_CLOCK_GATING_REG_OFFSET);
	clk_gate &= ~(1 << clk->clk_num);
	axi2cfg_writel(clk_gate, AXI2CFG_CLOCK_GATING_REG_OFFSET);
	spin_unlock_irqrestore(&pc30xx_clk_lock, flags);
}

/*
 * pll_calc_params() - calculate divf and divq for a given target rate.
 *
 * Calculate the PLL programming parameters to achieve a target frequency.
 * Returns the actual frequency that will be generated as there may be some
 * error margin.  All frequencies in Hz.
 *
 * @target:	The target rate for the PLL generator.
 * @ref_freq:	The input frequency to the device.
 * @divf_out:	Destination for the divf value.
 * @divq_out:	Destination for the divq value.
 */
static unsigned long pll_calc_params(unsigned long target,
				     unsigned long ref_freq,
				     unsigned long *divf_out,
				     unsigned long *divq_out)
{
	u64 target64 = target, divf, divq, divfn;
	unsigned long best_delta = ~0, rate = 0;
	unsigned n;

	/* Iterate over power of 2 divq values in the range 2..64. */
	for (n = 1; n <= 6; ++n) {
		unsigned long long vco_freq;

		/*
		 * We want to get a VCO output frequency in between 1.8GHz and
		 * 3.6GHz to generate the best output.
		 */
		divq = 1 << n;
		vco_freq = target64 * divq;
		if (vco_freq < 1800000000LLU || vco_freq > 3600000000LLU)
			continue;

		/* Calculate the initial divf value. */
		divf = (1LLU << 32) * ref_freq;
		do_div(divf, target * divq);

		/*
		 * Try different divf values in the range [divf - 1, divf + 1]
		 * to get the minimum error.
		 */
		for (divfn = divf - 1; divfn <= divf + 1; ++divfn) {
			unsigned long long divisor = divq * divfn;
			unsigned long long outf = ref_freq * (1LLU << 32);
			unsigned long delta;

			do_div(outf, divisor);
			delta = abs64(target - outf);
			if (delta < best_delta) {
				if (divf_out)
					*divf_out = divfn;
				if (divq_out)
					*divq_out = n;
				best_delta = delta;
				rate = outf;
			}
		}
	}

	return rate;
}

static long pc30xx_pll_round_rate(struct clk *clk, unsigned long rate)
{
	return (long)pll_calc_params(rate, clk_get_rate(clk->parent),
				     NULL, NULL);
}

static int pc30xx_pll_set_rate(struct clk *clk, unsigned long target)
{
	unsigned long divf, divq, flags;
	struct pc30xx_pll *pll = to_pc30xx_pll(clk);

	pll_calc_params(target, clk_get_rate(clk->parent), &divf, &divq);

	spin_lock_irqsave(&pc30xx_clk_lock, flags);

	axi2cfg_writel(divf, pll->set_base + PLL_DIVF_REG_OFFSET);
	axi2cfg_writel(divq, pll->set_base + PLL_DIVQ_REG_OFFSET);
	axi2cfg_writel(PLL_CHANGE_START_MASK,
		       pll->set_base + PLL_CHANGE_REG_OFFSET);
	while (axi2cfg_readl(pll->set_base + PLL_CHANGE_REG_OFFSET) &
	       PLL_CHANGE_ACTIVE_MASK)
		cpu_relax();

	spin_unlock_irqrestore(&pc30xx_clk_lock, flags);

	return 0;
}

/*
 * Get the rate of a PLL in pc30xx.  The frequency sense macro returns the
 * frequency based on a 20MHz reference clock but the reference clock may not
 * be 20MHz so we scale the sensed frequency.
 */
static int pc30xx_pll_get_rate(struct clk *clk)
{
	struct pc30xx_pll *pll = to_pc30xx_pll(clk);
	unsigned long sense, flags, parent_rate = clk_get_rate(clk->parent);
	unsigned long rate = 0;
	u64 rate64;

	spin_lock_irqsave(&pc30xx_clk_lock, flags);
	while (rate == 0) {
		axi2cfg_writel(PLL_SENSE_START_MASK, pll->sense_reg);

		while ((sense = axi2cfg_readl(pll->sense_reg)) &
		       PLL_SENSE_ACTIVE_MASK)
			cpu_relax();

		if (sense & PLL_SENSE_FREQ_VALID_MASK) {
			rate = (sense & PLL_SENSE_FREQ_MASK) * 1000000;
			break;
		}
	}
	spin_unlock_irqrestore(&pc30xx_clk_lock, flags);

	rate64 = (u64)rate * parent_rate;
	do_div(rate64, 20000000LLU);

	return (int)rate64;
}

/*
 * The gateable clocks all get their frequency from their parent PLLs.
 */
static int pc30xx_clk_get_rate(struct clk *clk)
{
	if (WARN_ON(!clk->parent))
		return -ENODEV;

	return clk_get_rate(clk->parent);
}

static struct clk_ops pc30xx_fixed_clk_ops = {
	.enable		= pc30xx_clk_enable,
	.disable	= pc30xx_clk_disable,
	.is_enabled	= pc30xx_clk_is_enabled,
	.get_rate	= pc30xx_clk_get_rate,
};

static struct clk_ops pc30xx_variable_clk_ops = {
	.round_rate	= pc30xx_pll_round_rate,
	.set_rate	= pc30xx_pll_set_rate,
	.get_rate	= pc30xx_pll_get_rate,
};

static int pc30xx_ref_clk_get_rate(struct clk *clk)
{
	unsigned long input_xtal_clk =
		axi2cfg_readl(AXI2CFG_INPUT_XTAL_CLOCK_REG_OFFSET);

	switch (input_xtal_clk) {
	case 0x0:
		return 19200000;
	case 0x1:
		return 20000000;
	case 0x2:
		return 26000000;
	default:
		panic("Unsupported reference clock frequency");
	}
}

static struct clk_ops pc30xx_ref_clk_ops = {
	.get_rate	= pc30xx_ref_clk_get_rate,
};

/*
 * Use a variable clk.  These clocks aren't directly variable themselves but
 * they do need to get their rate from the parent PLL so the rate can change.
 */
#define PC30XX_CLK(__name, __indx) \
	VARIABLE_CLK(__name, __indx, 1, 1, 1, &pc30xx_fixed_clk_ops)

PC30XX_CLK(tzprot,	0);
PC30XX_CLK(spi,		1);
PC30XX_CLK(dmac0,	2);
PC30XX_CLK(dmac1,	3);
PC30XX_CLK(ebi,		4);
PC30XX_CLK(ipsec,	5);
PC30XX_CLK(l2_engine,	6);
PC30XX_CLK(trng,	7);
PC30XX_CLK(fuse,	8);
PC30XX_CLK(otp,		9);
PC30XX_CLK(cascade,	10);
PC30XX_CLK(nand,	11);
PC30XX_CLK(memif_arm,	12);
PC30XX_CLK(shd_sdram,	13);
PC30XX_CLK(shd_sram,	14);
PC30XX_CLK(axi2pico,	15);
PC30XX_CLK(dummy,	-1);
VARIABLE_CLK(ref,			 -1, 10000000, 30000000, 100000, &pc30xx_ref_clk_ops);

/*	   Name		Min (Hz)   (Max Hz)    Set   Sense */
PC30XX_PLL(arm,		140000000, 1000000000, 0x70, 0x50);
PC30XX_PLL(amba,	200000000, 200000000,  0x80, 0x54);
PC30XX_PLL(ddr,		533000000, 533000000,  0x90, 0x58);
PC30XX_PLL(pico,	160000000, 160000000,  0xa0, 0x5c);

static struct clk *pc30xx_clks[] = {
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
	&cascade_clk,
	&nand_clk,
	&memif_arm_clk,
	&shd_sdram_clk,
	&shd_sram_clk,
	&axi2pico_clk,
	&dummy_clk,
	&arm_clk.clk,
	&amba_clk.clk,
	&pico_clk.clk,
	&ddr_clk.clk,
	&ref_clk,
};

static struct clk_lookup pc30xx_clk_lookup[] = {
	CLK_LOOKUP(NULL,		"tzprot_ctl",	&tzprot_clk),
	CLK_LOOKUP("dw_spi_mmio.0",	NULL,		&spi_clk),
	CLK_LOOKUP("dw_dmac.0",		NULL,		&dmac0_clk),
	CLK_LOOKUP("dw_dmac.1",		NULL,		&dmac1_clk),
	CLK_LOOKUP(NULL,		"ebi",		&ebi_clk),
	CLK_LOOKUP("picoxcell-ipsec",	NULL,		&ipsec_clk),
	CLK_LOOKUP("picoxcell-l2",	NULL,		&l2_engine_clk),
	CLK_LOOKUP("picoxcell-trng",	NULL,		&trng_clk),
	CLK_LOOKUP("picoxcell-fuse",	NULL,		&fuse_clk),
	CLK_LOOKUP("picoxcell-otp-pc30xx", NULL,	&otp_clk),
	CLK_LOOKUP("dw_wdt",		NULL,		&dummy_clk),
	CLK_LOOKUP("macb",		"pclk",		&dummy_clk),
	CLK_LOOKUP("macb",		"hclk",		&dummy_clk),
	CLK_LOOKUP(NULL,		"arm",		&arm_clk.clk),
	CLK_LOOKUP("dw_apb_timer.0",	NULL,		&dummy_clk),
	CLK_LOOKUP("dw_apb_timer.1",	NULL,		&dummy_clk),
	CLK_LOOKUP(NULL,		"uart",		&dummy_clk),
};

static void __init pc30xx_clk_init(void)
{
	static struct clk *amba_clks[] = {
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
		&cascade_clk,
		&nand_clk,
		&axi2pico_clk,
		&dummy_clk,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(amba_clks); ++i) {
		__clk_disable(amba_clks[i]);
		clk_set_parent(amba_clks[i], &amba_clk.clk);
	}

	clk_set_parent(&memif_arm_clk, &ddr_clk.clk);
	clk_set_parent(&shd_sdram_clk, &ddr_clk.clk);

	clk_set_parent(&shd_sram_clk, &amba_clk.clk);
	clk_set_parent(&axi2pico_clk, &amba_clk.clk);

	clk_set_parent(&amba_clk.clk, &ref_clk);
	clk_set_parent(&pico_clk.clk, &ref_clk);
	clk_set_parent(&arm_clk.clk, &ref_clk);
	clk_set_parent(&ddr_clk.clk, &ref_clk);

	for (i = 0; i < ARRAY_SIZE(pc30xx_clks); ++i)
		picoxcell_clk_add(pc30xx_clks[i]);

	clkdev_add_table(pc30xx_clk_lookup, ARRAY_SIZE(pc30xx_clk_lookup));
}

static void pc30xx_init_bus_snoopers(void)
{
	static const char *pc30xx_snoop_err_names[32] = {
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
		[10]	= "nand (read)",
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
		[22]	= "nand (write)",
		[23]	= "ipsec (write)",
	};

	static struct resource irq = {
		.start	= IRQ_PC30XX_BUS_ERR,
		.end	= IRQ_PC30XX_BUS_ERR,
		.flags	= IORESOURCE_IRQ,
	};

	platform_device_register_resndata(NULL, "picoxcell-bus-error", -1,
					  &irq, 1, pc30xx_snoop_err_names,
					  sizeof(pc30xx_snoop_err_names));
}

static void pc30xx_add_spaccs(void)
{
	picoxcell_add_spacc("picoxcell-ipsec-v2", PICOXCELL_IPSEC_BASE,
			    IRQ_IPSEC, -1);
	picoxcell_add_spacc("picoxcell-l2-v2", PICOXCELL_CIPHER_BASE,
			    IRQ_AES, -1);
}

static void pc30xx_init_cpufreq(void)
{
	if (picoxcell_cpufreq_init(140000, 1000000))
		pr_err("failed to init cpufreq for pc30xx\n");
}

#ifdef CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND
static inline void pc30xx_pm_stop_wdt(void)
{
	unsigned long syscfg = axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET);

	syscfg |= (1 << AXI2CFG_SYSCFG_WDG_PAUSE_IDX);

	axi2cfg_writel(syscfg, AXI2CFG_SYSCFG_REG_OFFSET);
}

static inline void pc30xx_pm_restore_wdt(void)
{
	unsigned long syscfg = axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET);

	syscfg &= ~(1 << AXI2CFG_SYSCFG_WDG_PAUSE_IDX);

	axi2cfg_writel(syscfg, AXI2CFG_SYSCFG_REG_OFFSET);
}
#else /* CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND */
static inline void pc30xx_pm_stop_wdt(void) {}
static inline void pc30xx_pm_restore_wdt(void) {}
#endif /* CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND */

static void pc30xx_init_pm(void)
{
	picoxcell_init_pm(pc30xx_pm_stop_wdt, pc30xx_pm_restore_wdt);
}

static const char * const pc30xx_sdgpio_pins[] = {
	"sdgpio0",
	"sdgpio1",
	"sdgpio2",
	"sdgpio3",
	"sdgpio4",
	"sdgpio5",
	"sdgpio6",
	"sdgpio7",
	"sdgpio8",
	"sdgpio9",
	"sdgpio10",
	"sdgpio11",
	"sdgpio12",
	"sdgpio13",
	"sdgpio14",
	"sdgpio15",
	"sdgpio16",
	"sdgpio17",
	"sdgpio18",
	"sdgpio19",
	"sdgpio20",
	"sdgpio21",
	"sdgpio22",
	"sdgpio23",
};

static const struct sdgpio_platform_data pc30xx_sdgpio = {
	.banks				= {
		{
			.names		= pc30xx_sdgpio_pins,
			.block_base	= 0,
			.gpio_start	= PC30XX_GPIO_PIN_SDGPIO_0,
			.nr_pins	= ARRAY_SIZE(pc30xx_sdgpio_pins),
			.label		= "sdgpio",
		},
	},
	.nr_banks			= 1,
};

static void pc30xx_add_gpio(void)
{
	picoxcell_add_gpio_port(0, 8, PC30XX_GPIO_PIN_ARM_0);
	picoxcell_add_gpio_port(1, 32, PC30XX_GPIO_PIN_ARM_8);
	picoxcell_add_gpio_port(2, 23, PC30XX_GPIO_PIN_ARM_40);
}

static void __init pc30xx_init(void)
{
	pc30xx_init_bus_snoopers();
	pc30xx_add_spaccs();
	pc30xx_init_cpufreq();
	pc30xx_init_pm();
	pc30xx_add_gpio();
}

const struct picoxcell_soc pc30xx_soc __initconst = {
	.init		= pc30xx_init,
	.init_clocks	= pc30xx_clk_init,
};

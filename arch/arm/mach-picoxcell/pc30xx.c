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
#include <linux/platform_data/pc30xxts.h>
#include <linux/platform_data/picoxcell_fuse.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "mux.h"
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

static long pc30xx_pll_round_freq(long freq)
{
	long error;

	/*
	 * Round to the nearest MHz to account for small error in the PLL.  We
	 * don't set any PLL's to a sub 1MHz division.
	 */
	error = freq % 1000000;
	if (error < 500000)
		return freq - error;
	else
		return freq + 1000000 - error;
}

static long pc30xx_pll_round_rate(struct clk *clk, unsigned long rate)
{
	long freq = (long)pll_calc_params(rate, clk_get_rate(clk->parent),
					  NULL, NULL);

	return pc30xx_pll_round_freq(freq);
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

	return (int)pc30xx_pll_round_freq((long)rate64);
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
	CLK_LOOKUP("picoxcell-spi.0",	NULL,		&spi_clk),
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
	CLK_LOOKUP("denali-nand-mmio",	NULL,		&nand_clk),
	CLK_LOOKUP("picoArray.0",	"axi2pico",	&axi2pico_clk),
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
	picoxcell_add_spacc("picoxcell-ipsec-v2", PC30XX_IPSEC_BASE,
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

static const char *const pc30xx_porta_names[] = {
	"usim_clk",
	"usim_io",
	"usim_vcc",
	"usim_rst",
	"usim_cd",
	"sw_rst",
	"per_rst",
	"mii_phy_irq",
};

static const char *const pc30xx_portb_names[] = {
	"arm8",
	"arm9",
	"arm10",
	"arm11",
	"arm12",
	"arm13",
	"gps_hclk",
	"arm15",
	"gps_do",
	"gps_clk",
	"gps_di",
	"gps_frm",
	"arm20",
	"arm21",
	"arm22",
	"arm23",
	"arm24",
	"arm25",
	"arm26",
	"arm27",
	"arm28",
	"arm29",
	"arm30",
	"arm31",
	"arm32",
	"arm33",
	"arm34",
	"arm35",
	"arm36",
	"arm37",
	"arm38",
	"arm39",
};

static const char *const pc30xx_portc_names[] = {
	"arm40",
	"arm41",
	"arm42",
	"arm43",
	"arm44",
	"arm45",
	"arm46",
	"arm47",
	"arm48",
	"arm49",
	"arm50",
	"arm51",
	"arm52",
	"arm53",
	"arm54",
	"arm55",
	"arm56",
	"arm57",
	"arm58",
	"arm59",
	"arm60",
	"arm61",
	"arm62",
};

static void pc30xx_add_gpio(void)
{
	picoxcell_add_gpio_port(0, 8, PC30XX_GPIO_PIN_ARM_0,
				pc30xx_porta_names);
	picoxcell_add_gpio_port(1, 32, PC30XX_GPIO_PIN_ARM_8,
				pc30xx_portb_names);
	picoxcell_add_gpio_port(2, 23, PC30XX_GPIO_PIN_ARM_40,
				pc30xx_portc_names);
}

static struct picoxcell_fuse_map pc30xx_fuse_map = {
	.nr_fuses	= 4096,
	.ltp_fuse	= 994,
	.ranges		= {
		FUSE_RANGE_PROTECTED(secure_bootstrap, 0, 127, 928, 938, 948),
		FUSE_RANGE_PROTECTED(counter_iv, 128, 255, 929, 939, 949),
		FUSE_RANGE_PROTECTED(key2, 256, 383, 930, 940, 950),
		FUSE_RANGE_PROTECTED(key3, 384, 511, 931, 941, 951),
		FUSE_RANGE_PROTECTED(key4, 512, 639, 932, 942, 952),
		FUSE_RANGE_PROTECTED(key5, 640, 767, 933, 943, 953),
		FUSE_RANGE_PROTECTED(die_ident, 768, 895, 934, 944, 954),
		FUSE_RANGE_PROTECTED(temp_cal_offset, 896, 903, 934, 944, 954),
		FUSE_RANGE_PROTECTED(partition1, 1024, 2047, 935, 945, 955),
		FUSE_RANGE_PROTECTED(partition2, 2048, 3071, 936, 946, 956),
		FUSE_RANGE_PROTECTED(partition3, 3072, 4095, 937, 947, 957),
		FUSE_RANGE(secure_boot, 992, 992),
		FUSE_RANGE(disable_tz, 993, 993),
		FUSE_RANGE(global_ltp, 994, 994),
		FUSE_RANGE(disable_debug, 995, 995),
		FUSE_RANGE(disable_isc, 996, 996),
		FUSE_RANGE(disable_jtag, 997, 997),
		FUSE_RANGE(disable_invasive_debug, 998, 998),
		FUSE_RANGE(disable_noninvasive_debug, 999, 999),
		FUSE_RANGE(disable_cp15, 1000, 1000),
		FUSE_RANGE(disable_memif_arm, 1001, 1001),
		FUSE_RANGE(disable_nonsecure_parallel_flash, 1002, 1002),
		FUSE_RANGE(global_otp_ltp, 1015, 1015),
		FUSE_RANGE(otp_disable_jtag, 1016, 1016),
		FUSE_RANGE(otp_boot_mode, 1017, 1018),
		FUSE_RANGE(otp_direct_io_disable, 1021, 1021),
		FUSE_RANGE(otp_robp1, 1003, 1003),
		FUSE_RANGE(otp_robp2, 1004, 1004),
		FUSE_RANGE(otp_robp3, 1005, 1005),
		FUSE_RANGE(otp_robp4, 1006, 1006),
		FUSE_RANGE(otp_ltp1, 1007, 1007),
		FUSE_RANGE(otp_ltp2, 1008, 1008),
		FUSE_RANGE(otp_ltp3, 1009, 1009),
		FUSE_RANGE(otp_ltp4, 1010, 1010),
		FUSE_RANGE(otp_disable_jtag1, 1011, 1011),
		FUSE_RANGE(otp_disable_jtag2, 1012, 1012),
		FUSE_RANGE(otp_disable_jtag3, 1013, 1013),
		FUSE_RANGE(otp_disable_jtag4, 1014, 1014),
		FUSE_RANGE_NULL,
	},
};

static void pc30xx_add_fuse(void)
{
	picoxcell_add_fuse(&pc30xx_fuse_map);
}

static u8 pc30xx_temp_cal(void)
{
#define TEMP_CAL_FUSE	896
	u8 temp_cal;

	if (picoxcell_fuse_read(TEMP_CAL_FUSE / 8, &temp_cal, 1)) {
		pr_err("failed to read temperature calibration offset\n");
		return 0;
	}

	return temp_cal;
}

static void pc30xx_add_ts(void)
{
	struct resource res = {
		.start	= PICOXCELL_AXI2CFG_BASE + 0xB0,
		.end	= PICOXCELL_AXI2CFG_BASE + 0xB7,
		.flags	= IORESOURCE_MEM,
	};
	struct pc30xxts_pdata pdata = {
		.trim	= pc30xx_temp_cal(),
	};
	platform_device_register_resndata(NULL, "pc30xxts", -1, &res, 1,
					  &pdata, sizeof(pdata));
}

static void pc30xx_add_otp(void)
{
	struct resource res = {
		.start	= PC30XX_OTP_BASE,
		.end	= PC30XX_OTP_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	};
	platform_device_register_simple("picoxcell-otp-pc30xx", -1, &res, 1);
}

static struct mux_def pc30xx_hnb_mux[] = {
	/*	Name		ARM	SD	PERIPH	REG	BIT	PERREG	PERBIT	FLAGS */
	MUXGPIO(usim_clk,	0,	16,	USIM,	0x34,	0,	0xc0,	4,	MUX_INVERT_PERIPH),
	MUXGPIO(usim_io,	1,	17,	USIM,	0x34,	1,	0xc0,	1,	MUX_INVERT_PERIPH),
	MUXGPIO(usim_vcc,	2,	18,	RSVD,	0x34,	2,	-1,	-1,	0),
	MUXGPIO(usim_rst,	3,	19,	RSVD,	0x34,	3,	-1,	-1,	0),
	MUXGPIO(usim_cd,	4,	20,	RSVD,	0x34,	4,	-1,	-1,	0),
	MUXGPIO(shd_gpio5,	5,	21,	RSVD,	0x34,	5,	-1,	-1,	0),
	MUXGPIO(shd_gpio6,	6,	22,	RSVD,	0x34,	6,	-1,	-1,	0),
	MUXGPIO(shd_gpio7,	7,	23,	RSVD,	0x34,	7,	-1,	-1,	0),
	MUXGPIO(shd_gpio8,	8,	8,	RSVD,	0x34,	8,	-1,	-1,	0),
	MUXGPIO(shd_gpio9,	9,	9,	RSVD,	0x34,	9,	-1,	-1,	0),
	MUXGPIO(shd_gpio10,	10,	10,	RSVD,	0x34,	10,	-1,	-1,	0),
	MUXGPIO(shd_gpio11,	11,	11,	RSVD,	0x34,	11,	-1,	-1,	0),
	MUXGPIO(shd_gpio12,	12,	12,	RSVD,	0x34,	12,	-1,	-1,	0),
	MUXGPIO(shd_gpio13,	13,	13,	RSVD,	0x34,	13,	-1,	-1,	0),
	MUXGPIO(shd_gpio14,	14,	14,	RSVD,	0x34,	14,	-1,	-1,	0),
	MUXGPIO(shd_gpio15,	15,	15,	FRACN,	0x34,	15,	0,	7,	MUX_INVERT_PERIPH),
	MUXGPIO(boot_mode0,	16,	0,	RSVD,	0x34,	16,	-1,	-1,	0),
	MUXGPIO(boot_mode1,	17,	1,	RSVD,	0x34,	17,	-1,	-1,	0),
	MUXGPIO(input_clk_sel0,	18,	2,	RSVD,	0x34,	18,	-1,	-1,	0),
	MUXGPIO(input_clk_sel1,	19,	3,	RSVD,	0x34,	19,	-1,	-1,	0),
	MUXGPIO(ssi_data_out,	22,	6,	SSI,	0x34,	22,	0x44,	0,	0),
	MUXGPIO(ssi_clk,	23,	7,	SSI,	0x34,	23,	0x44,	0,	0),
	MUXGPIO(ssi_data_in,	24,	-1,	SSI,	-1,	-1,	0x44,	0,	0),
	MUXGPIO(decode0,	25,	-1,	EBI,	-1,	-1,	0x40,	0,	0),
	MUXGPIO(decode1,	26,	-1,	EBI,	-1,	-1,	0x40,	1,	0),
	MUXGPIO(ebi_clk,	29,	-1,	EBI,	-1,	-1,	0x3c,	13,	0),
	MUXGPIO(pai_tx_data0,	47,	-1,	PAI,	-1,	-1,	0x38,	0,	0),
	MUXGPIO(pai_tx_data1,	48,	-1,	PAI,	-1,	-1,	0x38,	1,	0),
	MUXGPIO(pai_tx_data2,	49,	-1,	PAI,	-1,	-1,	0x38,	2,	0),
	MUXGPIO(pai_tx_data3,	50,	-1,	PAI,	-1,	-1,	0x38,	3,	0),
	MUXGPIO(pai_tx_data4,	51,	-1,	PAI,	-1,	-1,	0x38,	4,	0),
	MUXGPIO(pai_tx_data5,	52,	-1,	PAI,	-1,	-1,	0x38,	5,	0),
	MUXGPIO(pai_tx_data6,	53,	-1,	PAI,	-1,	-1,	0x38,	6,	0),
	MUXGPIO(pai_tx_data7,	54,	-1,	PAI,	-1,	-1,	0x38,	7,	0),
	MUXGPIO(pai_rx_data0,	55,	-1,	PAI,	-1,	-1,	0x38,	8,	0),
	MUXGPIO(pai_rx_data1,	56,	-1,	PAI,	-1,	-1,	0x38,	9,	0),
	MUXGPIO(pai_rx_data2,	57,	-1,	PAI,	-1,	-1,	0x38,	10,	0),
	MUXGPIO(pai_rx_data3,	58,	-1,	PAI,	-1,	-1,	0x38,	11,	0),
	MUXGPIO(pai_rx_data4,	59,	-1,	PAI,	-1,	-1,	0x38,	12,	0),
	MUXGPIO(pai_rx_data5,	60,	-1,	PAI,	-1,	-1,	0x38,	13,	0),
	MUXGPIO(pai_rx_data6,	61,	-1,	PAI,	-1,	-1,	0x38,	14,	0),
	MUXGPIO(pai_rx_data7,	62,	-1,	PAI,	-1,	-1,	0x38,	15,	0),

	/*	   Name			Periph	PeriphB	Reg	Bit */
	MUX2PERIPH(pad_pai_tx_clk,	PAI,	MAXIM,	0x4c,	0),
	MUX2PERIPH(pad_pai_tx_ctrl,	PAI,	MAXIM,	0x4c,	0),
	MUX2PERIPH(pad_pai_trig_clk,	PAI,	MAXIM,	0x4c,	0),
};

static enum mux_setting mii_get_mux(const struct mux_def *def)
{
	unsigned long idr = axi2cfg_readl(AXI2CFG_ID_REG_OFFSET);

	/* Bits 8:6 tell us the MII mode that we're using. */
	switch ((idr >> 6) & 0x7) {
	case 0x3:
	case 0x7:
		return MUX_ARM;
	default:
		return MUX_PERIPHERAL_MII;
	}
}

static struct mux_def pc30xx_labs_mux[] __used = {
	/*	Name		ARM	SD	PERIPH	REG	BIT	PERREG	PERBIT	FLAGS */
	MUXGPIO(mii_mode0,	20,	4,	RSVD,	0x34,	20,	-1,	-1,	0),
	MUXGPIO(mii_mode1,	21,	5,	RSVD,	0x34,	21,	-1,	-1,	0),
	MUXGPIO(decode2,	27,	-1,	EBI,	-1,	-1,	0x40,	2,	0),
	MUXGPIO(decode3,	28,	-1,	EBI,	-1,	-1,	0x40,	3,	0),
	MUXGPIO(ebi_addr14,	30,	-1,	EBI,	-1,	-1,	0x3c,	0,	0),
	MUXGPIO(ebi_addr15,	31,	-1,	EBI,	-1,	-1,	0x3c,	1,	0),
	MUXGPIO(ebi_addr16,	32,	-1,	EBI,	-1,	-1,	0x3c,	2,	0),
	MUXGPIO(ebi_addr17,	33,	-1,	EBI,	-1,	-1,	0x3c,	3,	0),
	MUXGPIO(ebi_addr18,	34,	-1,	EBI,	-1,	-1,	0x3c,	4,	0),
	MUXGPIO(ebi_addr19,	35,	-1,	EBI,	-1,	-1,	0x3c,	5,	0),
	MUXGPIO(ebi_addr20,	36,	-1,	EBI,	-1,	-1,	0x3c,	6,	0),
	MUXGPIO(ebi_addr21,	37,	-1,	EBI,	-1,	-1,	0x3c,	7,	0),
	MUXGPIO(ebi_addr22,	38,	-1,	EBI,	-1,	-1,	0x3c,	8,	0),
	MUXGPIO(ebi_addr23,	39,	-1,	EBI,	-1,	-1,	0x3c,	9,	0),

	/*	    Name,		ARM,	SD,	PERIPH,	get */
	MUXGPIOFUNC(mii_tx_data2,	40,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_tx_data3,	41,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_rx_data2,	42,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_rx_data3,	43,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_col,		44,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_crs,		45,	-1,	MII,	mii_get_mux),
	MUXGPIOFUNC(mii_tx_clk,		46,	-1,	MII,	mii_get_mux),
};

static void pc30xx_add_trng(void)
{
	picoxcell_add_trng(PC3X3_RNG_BASE);
}

static void __init pc30xx_init(void)
{
	unsigned long device_id = axi2cfg_readl(AXI2CFG_DEVICE_ID_REG_OFFSET);

	switch (device_id) {
	case 0x30:
		picoxcell_mux_register(pc30xx_hnb_mux,
				       ARRAY_SIZE(pc30xx_hnb_mux));
		break;

	default:
		panic("Unsupported device variant %lx\n", device_id);
	}
	pc30xx_init_bus_snoopers();
	pc30xx_add_spaccs();
	pc30xx_init_cpufreq();
	pc30xx_init_pm();
	pc30xx_add_gpio();
	pc30xx_add_fuse();
	pc30xx_add_ts();
	pc30xx_add_otp();
	pc30xx_add_trng();
}

const struct picoxcell_soc pc30xx_soc __initconst = {
	.init		= pc30xx_init,
	.init_clocks	= pc30xx_clk_init,
};

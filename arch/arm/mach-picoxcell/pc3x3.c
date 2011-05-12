/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
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
#include <linux/platform_data/picoxcell_fuse.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "mux.h"
#include "picoxcell_core.h"
#include "soc.h"

static DEFINE_SPINLOCK(pc3x3_clk_lock);

static int pc3x3_clk_is_enabled(struct clk *clk);
static void pc3x3_clk_disable(struct clk *clk);
static inline void pc3x3_clk_enable(struct clk *clk);
static long pc3x3_clk_round_rate(struct clk *clk, unsigned long rate);
static int pc3x3_clk_get_rate(struct clk *clk);
static int pc3x3_clk_set_rate(struct clk *clk, unsigned long target);

static struct clk_ops pc3x3_fixed_clk_ops = {
	.enable		= pc3x3_clk_enable,
	.disable	= pc3x3_clk_disable,
	.is_enabled	= pc3x3_clk_is_enabled,
};

static struct clk_ops pc3x3_variable_clk_ops = {
	.enable		= pc3x3_clk_enable,
	.disable	= pc3x3_clk_disable,
	.is_enabled	= pc3x3_clk_is_enabled,
	.round_rate	= pc3x3_clk_round_rate,
	.set_rate	= pc3x3_clk_set_rate,
	.get_rate	= pc3x3_clk_get_rate,
};

FIXED_CLK(tzprot,	CLOCK_TICK_RATE, 0, &pc3x3_fixed_clk_ops);
FIXED_CLK(spi,		CLOCK_TICK_RATE, 1, &pc3x3_fixed_clk_ops);
FIXED_CLK(dmac0,	CLOCK_TICK_RATE, 2, &pc3x3_fixed_clk_ops);
FIXED_CLK(dmac1,	CLOCK_TICK_RATE, 3, &pc3x3_fixed_clk_ops);
FIXED_CLK(ebi,		CLOCK_TICK_RATE, 4, &pc3x3_fixed_clk_ops);
FIXED_CLK(ipsec,	CLOCK_TICK_RATE, 5, &pc3x3_fixed_clk_ops);
FIXED_CLK(l2_engine,	CLOCK_TICK_RATE, 6, &pc3x3_fixed_clk_ops);
FIXED_CLK(trng,		CLOCK_TICK_RATE, 7, &pc3x3_fixed_clk_ops);
FIXED_CLK(fuse,		CLOCK_TICK_RATE, 8, &pc3x3_fixed_clk_ops);
FIXED_CLK(otp,		CLOCK_TICK_RATE, 9, &pc3x3_fixed_clk_ops);
FIXED_CLK(wdt,		CLOCK_TICK_RATE, -1, &pc3x3_fixed_clk_ops);
FIXED_CLK(dummy,	CLOCK_TICK_RATE, -1, &pc3x3_fixed_clk_ops);
FIXED_CLK(ref,		20000000, -1, NULL);
VARIABLE_CLK(arm,			 -1, 140000000, 700000000, 5000000, &pc3x3_variable_clk_ops);

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
	axi2cfg_writel(((freq / 1000000) / 5) - 1,
		       AXI2CFG_ARM_PLL_CLKF_REG_OFFS);
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
			next_target = roundup(next_target * 1000, clk->step);
		} else {
			next_step = ((6 * current_khz) / 5) - current_khz;
			next_target =
				min(target - current_khz, next_step) +
				current_khz;
			next_target = ((next_target * 1000) / clk->step) *
				clk->step;
		}

		pc3x3_cpu_pll_set(next_target);
	}

	spin_unlock_irqrestore(&pc3x3_clk_lock, flags);
	ret = 0;

out:
	return ret;
}

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
	CLK_LOOKUP("picoxcell-otp-pc3x3", NULL,		&otp_clk),
	CLK_LOOKUP("dw_wdt",		NULL,		&wdt_clk),
	CLK_LOOKUP(NULL,		"arm",		&arm_clk),
	CLK_LOOKUP("macb",		"pclk",		&dummy_clk),
	CLK_LOOKUP("macb",		"hclk",		&dummy_clk),
	CLK_LOOKUP(NULL,		"ref",		&ref_clk),
	CLK_LOOKUP("dw_apb_timer.0",	NULL,		&dummy_clk),
	CLK_LOOKUP("dw_apb_timer.1",	NULL,		&dummy_clk),
	CLK_LOOKUP("picoArray.0",	"axi2pico",	&dummy_clk),
};

static void __init pc3x3_clk_init(void)
{
	int i;

	clkdev_add_table(pc3x3_clk_lookup, ARRAY_SIZE(pc3x3_clk_lookup));

	picoxcell_clk_add(&ref_clk);
	for (i = 0; i < ARRAY_SIZE(pc3x3_clks); ++i) {
		picoxcell_clk_add(pc3x3_clks[i]);
		clk_set_parent(pc3x3_clks[i], &ref_clk);
	}

	/*
	 * For PC3x3, disable the clocks that aren't required in the core
	 * code. The drivers will enable the clocks when they get initialised.
	 */
	__clk_disable(&tzprot_clk);
	__clk_disable(&spi_clk);
	__clk_disable(&dmac0_clk);
	__clk_disable(&dmac1_clk);
	__clk_disable(&ipsec_clk);
	__clk_disable(&l2_engine_clk);
	__clk_disable(&trng_clk);
	__clk_disable(&otp_clk);
	__clk_disable(&ebi_clk);
	__clk_disable(&fuse_clk);
}

static struct mux_def pc3x3_mux[] = {
	/*	Name		ARM	SD	PERIPH	REG	BIT	PERREG	PERBIT	FLAGS */
	MUXGPIO(arm_gpio0,	0,	16,	RSVD,	0x34,	0,	-1,	-1,	0),
	MUXGPIO(arm_gpio1,	1,	17,	RSVD,	0x34,	1,	-1,	-1,	0),
	MUXGPIO(arm_gpio2,	2,	18,	RSVD,	0x34,	2,	-1,	-1,	0),
	MUXGPIO(arm_gpio3,	3,	19,	RSVD,	0x34,	3,	-1,	-1,	0),
	MUXGPIO(shd_gpio,	8,	8,	RSVD,	0x34,	8,	-1,	-1,	0),
	MUXGPIO(boot_mode0,	9,	9,	RSVD,	0x34,	9,	-1,	-1,	0),
	MUXGPIO(boot_mode1,	10,	10,	RSVD,	0x34,	10,	-1,	-1,	0),
	MUXGPIO(sdram_speed_sel,11,	11,	RSVD,	0x34,	11,	-1,	-1,	0),
	MUXGPIO(mii_rev_en,	12,	12,	RSVD,	0x34,	12,	-1,	-1,	0),
	MUXGPIO(mii_rmii_en,	13,	13,	RSVD,	0x34,	13,	-1,	-1,	0),
	MUXGPIO(mii_speed_sel,	14,	14,	RSVD,	0x34,	14,	-1,	-1,	0),

	MUXGPIO(ebi_addr14,	32,	-1,	EBI,	-1,	-1,	0x3c,	0,	0),
	MUXGPIO(ebi_addr15,	33,	-1,	EBI,	-1,	-1,	0x3c,	1,	0),
	MUXGPIO(ebi_addr16,	34,	-1,	EBI,	-1,	-1,	0x3c,	2,	0),
	MUXGPIO(ebi_addr17,	35,	-1,	EBI,	-1,	-1,	0x3c,	3,	0),
	MUXGPIO(ebi_addr18,	20,	4,	EBI,	0x34,	20,	0x3c,	4,	0),
	MUXGPIO(ebi_addr19,	21,	5,	EBI,	0x34,	21,	0x3c,	5,	0),
	MUXGPIO(ebi_addr20,	22,	6,	EBI,	0x34,	22,	0x3c,	6,	0),
	MUXGPIO(ebi_addr21,	23,	7,	EBI,	0x34,	23,	0x3c,	7,	0),
	MUXGPIO(ebi_addr22,	4,	20,	EBI,	0x34,	4,	0x3c,	8,	0),
	MUXGPIO(ebi_addr23,	5,	21,	EBI,	0x34,	5,	0x3c,	9,	0),
	MUXGPIO(ebi_addr24,	6,	22,	EBI,	0x34,	6,	0x3c,	10,	0),
	MUXGPIO(ebi_addr25,	7,	23,	EBI,	0x34,	7,	0x3c,	11,	0),
	MUXGPIO(ebi_addr26,	15,	15,	EBI,	0x34,	15,	0x3c,	12,	0),
	MUXGPIO(ebi_clk_pin,	53,	-1,	EBI,	-1,	-1,	0x3c,	13,	0),

	MUXGPIO(pai_rx_data0,	20,	4,	PAI,	0x34,	20,	0x38,	8,	0),
	MUXGPIO(pai_rx_data1,	21,	5,	PAI,	0x34,	21,	0x38,	9,	0),
	MUXGPIO(pai_rx_data2,	22,	6,	PAI,	0x34,	22,	0x38,	10,	0),
	MUXGPIO(pai_rx_data3,	23,	7,	PAI,	0x34,	23,	0x38,	11,	0),
	MUXGPIO(pai_rx_data4,	28,	-1,	PAI,	-1,	-1,	0x38,	4,	0),
	MUXGPIO(pai_rx_data5,	29,	-1,	PAI,	-1,	-1,	0x38,	5,	0),
	MUXGPIO(pai_rx_data6,	30,	-1,	PAI,	-1,	-1,	0x38,	6,	0),
	MUXGPIO(pai_rx_data7,	31,	-1,	PAI,	-1,	-1,	0x38,	7,	0),

	MUXGPIO(pai_tx_data0,	4,	20,	PAI,	0x34,	4,	0x38,	0,	0),
	MUXGPIO(pai_tx_data1,	5,	21,	PAI,	0x34,	5,	0x38,	1,	0),
	MUXGPIO(pai_tx_data2,	6,	22,	PAI,	0x34,	6,	0x38,	2,	0),
	MUXGPIO(pai_tx_data3,	7,	23,	PAI,	0x34,	7,	0x38,	3,	0),
	MUXGPIO(pai_tx_data4,	24,	-1,	PAI,	-1,	-1,	0x38,	4,	0),
	MUXGPIO(pai_tx_data5,	25,	-1,	PAI,	-1,	-1,	0x38,	5,	0),
	MUXGPIO(pai_tx_data6,	26,	-1,	PAI,	-1,	-1,	0x38,	6,	0),
	MUXGPIO(pai_tx_data7,	27,	-1,	PAI,	-1,	-1,	0x38,	7,	0),

	MUXGPIO(decode0,	36,	-1,	DECODE,	-1,	-1,	0x40,	0,	0),
	MUXGPIO(decode1,	37,	-1,	DECODE,	-1,	-1,	0x40,	1,	0),
	MUXGPIO(decode2,	38,	-1,	DECODE,	-1,	-1,	0x40,	2,	0),
	MUXGPIO(decode3,	39,	-1,	DECODE,	-1,	-1,	0x40,	3,	0),

	MUXGPIO(ssi_clk,	40,	-1,	SSI,	-1,	-1,	0x44,	0,	0),
	MUXGPIO(ssi_data_in,	41,	-1,	SSI,	-1,	-1,	0x44,	0,	0),
	MUXGPIO(ssi_data_out,	42,	-1,	SSI,	-1,	-1,	0x44,	0,	0),

	MUXGPIO(mii_tx_data2,	43,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_tx_data3,	44,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_rx_data2,	45,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_rx_data3,	46,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_col,	47,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_crs,	48,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),
	MUXGPIO(mii_tx_clk,	49,	-1,	MII,	-1,	-1,	0,	13,	MUX_RO),

	MUXGPIO(max_tx_ctrl,	50,	-1,	MAXIM,	-1,	-1,	0x44,	1,	0),
	MUXGPIO(max_ref_clk,	51,	-1,	MAXIM,	-1,	-1,	0x44,	1,	0),
	MUXGPIO(max_trig_clk,	52,	-1,	MAXIM,	-1,	-1,	0x44,	1,	0),

	MUXGPIO(sdgpio0,	-1,	0,	FRACN,	-1,	-1,	0,	7,	MUX_INVERT_PERIPH),
};

static void pc3x3_init(void);

const struct picoxcell_soc pc3x3_soc __initconst = {
	.init		= pc3x3_init,
	.init_clocks	= pc3x3_clk_init,
};

static const char * const pc3x3_sdgpio_pins[] = {
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

static const struct sdgpio_platform_data pc3x3_sdgpio = {
	.banks				= {
		{
			.names		= pc3x3_sdgpio_pins,
			.block_base	= 0,
			.gpio_start	= PC3X3_GPIO_PIN_SDGPIO_0,
			.nr_pins	= ARRAY_SIZE(pc3x3_sdgpio_pins),
			.label		= "sdgpio",
		},
	},
	.nr_banks			= 1,
};

static const char *const pc3x3_porta_names[] = {
	"arm0",
	"arm1",
	"arm2",
	"arm3",
	"arm4",
	"arm5",
	"arm6",
	"arm7",
};

static const char *const pc3x3_portb_names[] = {
	"arm8",
	"arm9",
	"arm10",
	"arm11",
	"arm12",
	"arm13",
	"arm14",
	"arm15",
	"arm16",
	"arm17",
	"arm18",
	"arm19",
	"arm20",
	"arm21",
	"arm22",
	"arm23",
};

static const char *const pc3x3_portd_names[] = {
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
};

static void pc3x3_add_gpio(void)
{
	picoxcell_add_gpio_port(0, 8, PC3X3_GPIO_PIN_ARM_0,
				pc3x3_porta_names);
	picoxcell_add_gpio_port(1, 16, PC3X3_GPIO_PIN_ARM_8,
				pc3x3_portb_names);
	picoxcell_add_gpio_port(3, 30, PC3X3_GPIO_PIN_ARM_24,
				pc3x3_portd_names);
	platform_device_register_data(NULL, "sdgpio", -1, &pc3x3_sdgpio,
		sizeof(pc3x3_sdgpio));
}

/*
 * The fuse block contains an 8 bit number which is the maximum clkf value
 * that we can program. If this isn't programmed then allow 700Mhz operation.
 * If not, limit the maximum speed to whatever this value corresponds to.
 */
static unsigned int picoxcell_cpufreq_max_speed(void)
{
#define MAX_CLKF_FUSE	904
#define MAX_CLKF_REG	IO_ADDRESS(PICOXCELL_FUSE_BASE + 904 / 8)
	u8 max_clkf;
	struct clk *fuse;

	fuse = clk_get_sys("picoxcell-fuse", NULL);
	if (IS_ERR(fuse)) {
		pr_warn("no fuse clk, unable to get max cpu freq\n");
		max_clkf = 0;
		goto out;
	}

	if (clk_enable(fuse)) {
		pr_warn("unable to enable fuse clk, unable to get max cpu freq\n");
		max_clkf = 0;
		goto out_put;
	}

	max_clkf = readb(MAX_CLKF_REG);
	clk_disable(fuse);

out_put:
	clk_put(fuse);
out:
	return max_clkf ? ((max_clkf + 1) * 5) * 1000 : 700000;
}

static void pc3x3_init_cpufreq(void)
{
	if (picoxcell_cpufreq_init(140000, picoxcell_cpufreq_max_speed()))
		pr_err("failed to init cpufreq for pc3x3\n");
}

#ifdef CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND
static inline void pc3x3_pm_stop_wdt(void)
{
	unsigned long syscfg = axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET);

	syscfg |= (1 << AXI2CFG_SYSCFG_WDG_PAUSE_IDX);

	axi2cfg_writel(syscfg, AXI2CFG_SYSCFG_REG_OFFSET);
}

static inline void pc3x3_pm_restore_wdt(void)
{
	unsigned long syscfg = axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET);

	syscfg &= ~(1 << AXI2CFG_SYSCFG_WDG_PAUSE_IDX);

	axi2cfg_writel(syscfg, AXI2CFG_SYSCFG_REG_OFFSET);
}
#else /* CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND */
static inline void pc3x3_pm_stop_wdt(void) {}
static inline void pc3x3_pm_restore_wdt(void) {}
#endif /* CONFIG_PICOXCELL_STOP_WDT_IN_SUSPEND */

static void pc3x3_init_pm(void)
{
	picoxcell_init_pm(pc3x3_pm_stop_wdt, pc3x3_pm_restore_wdt);
}

static void pc3x3_add_otp(void)
{
	struct resource otp_mem = {
		.start		= PC3X3_OTP_BASE,
		.end		= PC3X3_OTP_BASE + SZ_32K - 1,
		.flags		= IORESOURCE_MEM,
	};

	platform_device_register_simple("picoxcell-otp-pc3x3", -1, &otp_mem, 1);
}

static void pc3x3_init_bus_snoopers(void)
{
	static const char *pc3x3_snoop_err_names[32] = {
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
					  pc3x3_snoop_err_names,
					  sizeof(pc3x3_snoop_err_names));
}

static void pc3x3_add_spaccs(void)
{
	picoxcell_add_spacc("picoxcell-ipsec", PICOXCELL_IPSEC_BASE,
			    IRQ_IPSEC, -1);
	picoxcell_add_spacc("picoxcell-l2", PICOXCELL_CIPHER_BASE,
			    IRQ_AES, -1);
}

static void pc3x3_add_trng(void)
{
	picoxcell_add_trng(PC3X3_RNG_BASE);
}

static struct picoxcell_fuse_map pc3x3_fuse_map = {
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

static void pc3x3_add_fuse(void)
{
	picoxcell_add_fuse(&pc3x3_fuse_map);
}

static void pc3x3_add_emac(void)
{
	picoxcell_add_emac(PICOXCELL_EMAC_BASE, IRQ_EMAC, 0);
}

static void __init pc3x3_init(void)
{
	picoxcell_mux_register(pc3x3_mux, ARRAY_SIZE(pc3x3_mux));
	pc3x3_add_gpio();
	pc3x3_init_cpufreq();
	pc3x3_init_pm();
	pc3x3_add_otp();
	pc3x3_init_bus_snoopers();
	pc3x3_add_spaccs();
	pc3x3_add_trng();
	pc3x3_add_fuse();
	pc3x3_add_emac();
}

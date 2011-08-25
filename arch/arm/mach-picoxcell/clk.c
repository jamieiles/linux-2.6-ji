/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "common.h"

struct picoxcell_clk {
	struct clk		*clk;
	struct list_head	head;
	struct device_node	*of_node;
};

static LIST_HEAD(picoxcell_clks);

static struct clk *picoxcell_dt_clk_get(struct device_node *np,
					const char *output_id,
					void *data)
{
	return data;
}

static void picoxcell_clk_add(const struct clk_hw_ops *ops,
			      struct clk_hw *hw, struct device_node *of_node)
{
	int err;
	struct clk *clk;
	struct picoxcell_clk *pclk;

	if (WARN_ON(!ops || !hw))
		return;

	clk = clk_register(NULL, ops, hw, of_node->full_name);
	if (!clk) {
		WARN(1, "failed to add clock %s\n", of_node->full_name);
		return;
	}

	err = of_clk_add_provider(of_node, picoxcell_dt_clk_get, clk);
	WARN(err, "failed to add clock for %s", of_node->full_name);

	pclk = kzalloc(sizeof(*pclk), GFP_KERNEL);
	if (!pclk)
		return;

	pclk->clk = clk;
	pclk->of_node = of_node;
	list_add_tail(&pclk->head, &picoxcell_clks);

	if (!of_get_property(of_node, "picochip,clk-no-disable", NULL))
		if (ops->disable)
			ops->disable(hw);
}

static void __init picoxcell_add_fixed_clk(struct device_node *np)
{
	u32 rate;
	struct clk_hw_fixed *fixed;

	if (of_property_read_u32(np, "clock-frequency", &rate)) {
		pr_err("no clock-frequency for %s\n", np->full_name);
		return;
	}

	fixed = kzalloc(sizeof(*fixed), GFP_KERNEL);
	if (WARN(!fixed, "unable to allocate fixed clk for %s\n",
		 np->full_name))
		return;

	fixed->rate = rate;
	picoxcell_clk_add(&clk_fixed_ops, &fixed->hw, np);
}

static void __init picoxcell_add_pc3x3_gated_clk(struct device_node *gate)
{
	struct clk_gate *clk;
	u32 disable_bit;
	struct device_node *np;
	void __iomem *reg = of_iomap(gate, 0);

	if (!reg) {
		pr_err("unable to map regs for clk gate\n");
		return;
	}

	/*
	 * The gate itself is a fixed clk and can output the individual clocks
	 * as children.
	 */
	picoxcell_add_fixed_clk(gate);

	for_each_child_of_node(gate, np) {
		if (of_property_read_u32(np, "picochip,clk-disable-bit",
					 &disable_bit)) {
			pr_err("no picochip,clk-disable-bit for %s\n",
			       np->full_name);
			continue;
		}

		clk = kzalloc(sizeof(*clk), GFP_KERNEL);
		if (!clk)
			panic("unable to allocate clk for %s\n",
			      np->full_name);

		clk->bit_idx = disable_bit;
		clk->reg = reg;

		picoxcell_clk_add(&clk_gate_set_disable_ops, &clk->hw, np);
	}
}

struct pc3x3_pll {
	struct clk_hw	hw;
	void __iomem	*regs;
	unsigned long	min_freq;
	unsigned long	max_freq;
};

static inline struct pc3x3_pll *to_pc3x3_pll(struct clk_hw *clk)
{
	return container_of(clk, struct pc3x3_pll, hw);
}

#define PC3X3_PLL_CLKF_REG_OFFS		0x00
#define PC3X3_PLL_FREQ_SENSE_REG_OFFS	0x04
#define PC3X3_PLL_FREQ_SENSE_VALID	(1 << 29)
#define PC3X3_PLL_FREQ_SENSE_ACTIVE	(1 << 30)
#define PC3X3_PLL_FREQ_SENSE_START	(1 << 31)
#define PC3X3_PLL_FREQ_SENSE_FREQ_MASK	0x3FF
#define PC3X3_PLL_STEP			5000000

static unsigned long pc3x3_pll_recalc_rate(struct clk_hw *clk)
{
	unsigned int mhz = 0;
	unsigned long sense_val;
	struct pc3x3_pll *pll = to_pc3x3_pll(clk);

	while (0 == mhz) {
		do {
			writel(PC3X3_PLL_FREQ_SENSE_START,
			       pll->regs + PC3X3_PLL_FREQ_SENSE_REG_OFFS);

			/* Wait for the frequency sense to complete. */
			do {
				sense_val =
					readl(pll->regs +
					      PC3X3_PLL_FREQ_SENSE_REG_OFFS);
			} while ((sense_val & PC3X3_PLL_FREQ_SENSE_ACTIVE));
		} while (!(sense_val & PC3X3_PLL_FREQ_SENSE_VALID));

		/* The frequency sense returns the frequency in MHz. */
		mhz = (sense_val & PC3X3_PLL_FREQ_SENSE_FREQ_MASK);
	}

	return mhz * 1000000;
}

static long pc3x3_pll_round_rate(struct clk_hw *clk, unsigned long rate)
{
	long ret;
	unsigned long offset;
	struct pc3x3_pll *pll = to_pc3x3_pll(clk);

	rate = clamp(rate, pll->min_freq, pll->max_freq);
	offset = rate % PC3X3_PLL_STEP;
	rate -= offset;

	if (offset > PC3X3_PLL_STEP - offset)
		ret = rate + PC3X3_PLL_STEP;
	else
		ret = rate;

	return ret;
}

static void pc3x3_pll_set(struct clk_hw *clk, unsigned long rate)
{
	struct pc3x3_pll *pll = to_pc3x3_pll(clk);
	unsigned long clkf = ((rate / 1000000) / 5) - 1;

	writel(clkf, pll->regs + PC3X3_PLL_CLKF_REG_OFFS);
	udelay(2);
}

static int pc3x3_pll_set_rate(struct clk_hw *clk, unsigned long target,
			      unsigned long *new_rate)
{
	unsigned long current_khz;

	target = pc3x3_pll_round_rate(clk, target);
	*new_rate = target;
	target /= 1000;

	pr_debug("set cpu clock rate to %luKHz\n", target);

	/*
	 * We can only reliably step by 20% at a time. We may need to
	 * do this in several iterations.
	 */
	while ((current_khz = pc3x3_pll_recalc_rate(clk) / 1000) != target) {
		unsigned long next_step, next_target;

		if (target < current_khz) {
			next_step = current_khz - ((4 * current_khz) / 5);
			next_target = current_khz -
				min(current_khz - target, next_step);
			next_target = roundup(next_target * 1000,
					      PC3X3_PLL_STEP);
		} else {
			next_step = ((6 * current_khz) / 5) - current_khz;
			next_target =
				min(target - current_khz, next_step) +
				current_khz;
			next_target = ((next_target * 1000) / PC3X3_PLL_STEP) *
				PC3X3_PLL_STEP;
		}

		pc3x3_pll_set(clk, next_target);
	}

	return 0;
}

static const struct clk_hw_ops pc3x3_pll_ops = {
	.recalc_rate	= pc3x3_pll_recalc_rate,
	.round_rate	= pc3x3_pll_round_rate,
	.set_rate	= pc3x3_pll_set_rate,
};

static void __init picoxcell_add_pc3x3_pll(struct device_node *np)
{
	struct pc3x3_pll *clk;
	u32 min, max;
	void __iomem *regs;

	if (of_property_read_u32(np, "picochip,min-freq", &min)) {
		pr_err("no picochip,min-freq for %s\n", np->full_name);
		return;
	}

	if (of_property_read_u32(np, "picochip,max-freq", &max)) {
		pr_err("no picochip,max-freq for %s\n", np->full_name);
		return;
	}

	regs = of_iomap(np, 0);
	if (!regs) {
		pr_err("unable to map regs for %s\n", np->full_name);
		return;
	}

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		panic("unable to allocate clk for %s\n", np->full_name);

	clk->regs = regs;
	clk->min_freq = min;
	clk->max_freq = max;

	picoxcell_clk_add(&pc3x3_pll_ops, &clk->hw, np);
}

static struct picoxcell_clk *picoxcell_find_clk(struct device_node *np)
{
	struct picoxcell_clk *pclk;

	list_for_each_entry(pclk, &picoxcell_clks, head)
		if (pclk->of_node == np)
			return pclk;

	return NULL;
}

static void picoxcell_build_clk_tree(void)
{
	struct picoxcell_clk *pclk;

	list_for_each_entry(pclk, &picoxcell_clks, head) {
		struct device_node *parent;
		struct picoxcell_clk *parent_clk;

		parent = of_parse_phandle(pclk->of_node, "ref-clock", 0);
		if (!parent)
			continue;

		parent_clk = picoxcell_find_clk(parent);
		if (!parent_clk) {
			pr_err("clk %s parent is not registered\n",
			       pclk->of_node->full_name);
			continue;
		}

		clk_set_parent(pclk->clk, parent_clk->clk);
	}
}

static const struct of_device_id picoxcell_clk_match[] = {
	{
		.compatible = "fixed-clock",
		.data = picoxcell_add_fixed_clk,
	},
	{
		.compatible = "picochip,pc3x3-clk-gate",
		.data = picoxcell_add_pc3x3_gated_clk,
	},
	{
		.compatible = "picochip,pc3x3-pll",
		.data = picoxcell_add_pc3x3_pll,
	},
	{ /* Sentinel */ }
};

void __init picoxcell_scan_clocks(void)
{
	struct device_node *np;

	for_each_matching_node(np, picoxcell_clk_match) {
		const struct of_device_id *match =
			of_match_node(picoxcell_clk_match, np);
		void (*add_clk)(struct device_node *) = match->data;

		add_clk(np);
	}

	picoxcell_build_clk_tree();
}

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

struct clk_ops;

#define CLKF_NO_DISABLE		BIT(0)

struct clk {
	const char			*name;
	int				enable_count;
	const struct clk_ops		*ops;
	struct clk			*parent;
	struct list_head		head;
	struct device_node		*of_node;
	unsigned long			flags;
};

struct clk_ops {
	unsigned long	(*get_rate)(struct clk *clk);
	long		(*round_rate)(struct clk *clk,
				      unsigned long rate);
	int		(*set_rate)(struct clk *clk,
				    unsigned long rate);
	void		(*enable)(struct clk *clk);
	void		(*disable)(struct clk *clk);
};

static DEFINE_SPINLOCK(clk_lock);
static LIST_HEAD(picoxcell_clks);

static struct clk *picoxcell_dt_clk_get(struct device_node *np,
					const char *output_id,
					void *data)
{
	return data;
}

static void picoxcell_clk_add(struct clk *clk)
{
	int err;

	BUG_ON(!clk || !clk->ops);

	list_add_tail(&clk->head, &picoxcell_clks);

	if (of_get_property(clk->of_node, "picochip,clk-no-disable", NULL))
		clk->flags |= CLKF_NO_DISABLE;

	err = of_clk_add_provider(clk->of_node, picoxcell_dt_clk_get, clk);
	WARN(err, "failed to add clock for %s", clk->of_node->full_name);
}

void picoxcell_disable_unused_clks(void)
{
	struct clk *clk;

	list_for_each_entry(clk, &picoxcell_clks, head)
		if (!clk->enable_count && !(clk->flags & CLKF_NO_DISABLE) &&
		    clk->ops->disable)
			clk->ops->disable(clk);
}

void picoxcell_enable_clks_for_reset(void)
{
	struct clk *clk;

	list_for_each_entry(clk, &picoxcell_clks, head)
		if (clk->ops->enable)
			clk->ops->enable(clk);
}

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->ops->get_rate ? clk->ops->get_rate(clk) : -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->ops->round_rate ?  clk->ops->round_rate(clk, rate) :
		clk_get_rate(clk);
}
EXPORT_SYMBOL_GPL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->ops->set_rate ?  clk->ops->set_rate(clk, rate) :
		-EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(clk_set_rate);

static int __clk_enable(struct clk *clk)
{
	if (++clk->enable_count > 0) {
		if (clk->parent)
			__clk_enable(clk->parent);
		if (clk->ops->enable)
			clk->ops->enable(clk);
	}

	return 0;
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&clk_lock, flags);
	ret = __clk_enable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (--clk->enable_count <= 0) {
		if (clk->parent)
			__clk_disable(clk->parent);
		if (!(clk->flags & CLKF_NO_DISABLE) &&
		    clk->ops->disable)
			clk->ops->disable(clk);
		WARN_ONCE(clk->enable_count < 0,
			  "unbalanced disable count for clk %s",
			  clk->of_node->full_name);
		clk->enable_count = 0;
	}
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL_GPL(clk_disable);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int err = -EINVAL;

	spin_lock_irqsave(&clk_lock, flags);
	if (!clk->parent) {
		clk->parent = parent;
		clk->parent->enable_count += clk->enable_count;
		err = 0;
	}
	spin_unlock_irqrestore(&clk_lock, flags);

	return err;
}
EXPORT_SYMBOL_GPL(clk_set_parent);

struct fixed_clk {
	struct clk	clk;
	unsigned long	rate;
};

static inline struct fixed_clk *to_fixed_clk(struct clk *clk)
{
	return container_of(clk, struct fixed_clk, clk);
}

static unsigned long fixed_clk_get_rate(struct clk *clk)
{
	struct fixed_clk *fixed = to_fixed_clk(clk);

	return fixed->rate;
}

static const struct clk_ops fixed_clk_ops = {
	.get_rate	= fixed_clk_get_rate,
};

static void __init picoxcell_add_fixed_clk(struct device_node *np)
{
	struct fixed_clk *clk;
	u32 rate;

	if (of_property_read_u32(np, "clock-frequency", &rate)) {
		pr_err("no clock-frequency for %s\n", np->full_name);
		return;
	}

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		panic("unable to allocate clk for %s\n", np->full_name);

	clk->clk.ops = &fixed_clk_ops;
	clk->clk.name = np->name;
	clk->clk.of_node = np;
	clk->rate = rate;

	picoxcell_clk_add(&clk->clk);
}

struct gated_clk {
	struct clk	clk;
	unsigned int	disable_mask;
	unsigned long	rate;
	void __iomem	*reg;
};

static inline struct gated_clk *to_gated_clk(struct clk *clk)
{
	return container_of(clk, struct gated_clk, clk);
}

static unsigned long gated_clk_get_rate(struct clk *clk)
{
	struct gated_clk *gated = to_gated_clk(clk);

	return gated->rate;
}

static void gated_clk_enable(struct clk *clk)
{
	struct gated_clk *gated = to_gated_clk(clk);
	unsigned long gate;

	gate = readl(gated->reg);
	gate &= ~gated->disable_mask;
	writel(gate, gated->reg);
}

static void gated_clk_disable(struct clk *clk)
{
	struct gated_clk *gated = to_gated_clk(clk);
	unsigned long gate;

	gate = readl(gated->reg);
	gate |= gated->disable_mask;
	writel(gate, gated->reg);
}

static const struct clk_ops gated_clk_ops = {
	.get_rate	= gated_clk_get_rate,
	.enable		= gated_clk_enable,
	.disable	= gated_clk_disable,
};

static void __init picoxcell_add_pc3x3_gated_clk(struct device_node *gate)
{
	struct gated_clk *clk;
	u32 rate, disable_bit;
	struct device_node *np;
	void __iomem *reg = of_iomap(gate, 0);

	if (!reg) {
		pr_err("unable to map regs for clk gate\n");
		return;
	}

	for_each_child_of_node(gate, np) {
		if (of_property_read_u32(np, "clock-frequency", &rate)) {
			pr_err("no clock-frequency for %s\n", np->full_name);
			continue;
		}

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

		clk->clk.ops = &gated_clk_ops;
		clk->clk.name = np->name;
		clk->clk.of_node = np;
		clk->rate = rate;
		clk->disable_mask = (1 << disable_bit);
		clk->reg = reg;

		picoxcell_clk_add(&clk->clk);
	}
}

struct pc3x3_pll {
	struct clk	clk;
	void __iomem	*regs;
	unsigned long	min_freq;
	unsigned long	max_freq;
};

static inline struct pc3x3_pll *to_pc3x3_pll(struct clk *clk)
{
	return container_of(clk, struct pc3x3_pll, clk);
}

#define PC3X3_PLL_CLKF_REG_OFFS		0x00
#define PC3X3_PLL_FREQ_SENSE_REG_OFFS	0x04
#define PC3X3_PLL_FREQ_SENSE_VALID	(1 << 29)
#define PC3X3_PLL_FREQ_SENSE_ACTIVE	(1 << 30)
#define PC3X3_PLL_FREQ_SENSE_START	(1 << 31)
#define PC3X3_PLL_FREQ_SENSE_FREQ_MASK	0x3FF
#define PC3X3_PLL_STEP			5000000

static unsigned long __pc3x3_pll_get_rate(struct clk *clk)
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

static unsigned long pc3x3_pll_get_rate(struct clk *clk)
{
	unsigned long flags, rate;

	spin_lock_irqsave(&clk_lock, flags);
	rate = __pc3x3_pll_get_rate(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return rate;
}

static long pc3x3_pll_round_rate(struct clk *clk, unsigned long rate)
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

static void pc3x3_pll_set(struct clk *clk, unsigned long rate)
{
	struct pc3x3_pll *pll = to_pc3x3_pll(clk);
	unsigned long clkf = ((rate / 1000000) / 5) - 1;

	writel(clkf, pll->regs + PC3X3_PLL_CLKF_REG_OFFS);
	udelay(2);
}

static int pc3x3_pll_set_rate(struct clk *clk, unsigned long target)
{
	unsigned long flags, current_khz;

	target = clk_round_rate(clk, target);
	target /= 1000;

	pr_debug("set cpu clock rate to %luKHz\n", target);

	spin_lock_irqsave(&clk_lock, flags);

	/*
	 * We can only reliably step by 20% at a time. We may need to
	 * do this in several iterations.
	 */
	while ((current_khz = __pc3x3_pll_get_rate(clk) / 1000) != target) {
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

	spin_unlock_irqrestore(&clk_lock, flags);

	return 0;
}

static const struct clk_ops pc3x3_pll_ops = {
	.get_rate	= pc3x3_pll_get_rate,
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

	clk->clk.ops = &pc3x3_pll_ops;
	clk->clk.name = np->name;
	clk->clk.of_node = np;
	clk->regs = regs;
	clk->min_freq = min;
	clk->max_freq = max;

	picoxcell_clk_add(&clk->clk);
}

static struct clk *picoxcell_find_clk(struct device_node *np)
{
	struct clk *clk;

	list_for_each_entry(clk, &picoxcell_clks, head)
		if (clk->of_node == np)
			return clk;

	return NULL;
}

static void picoxcell_build_clk_tree(void)
{
	struct clk *clk;

	list_for_each_entry(clk, &picoxcell_clks, head) {
		struct device_node *parent;
		struct clk *parent_clk;

		parent = of_parse_phandle(clk->of_node, "ref-clock", 0);
		if (!parent)
			continue;

		parent_clk = picoxcell_find_clk(parent);
		if (!parent_clk) {
			pr_err("clk %s parent is not registered\n",
			       clk->of_node->full_name);
			continue;
		}

		clk_set_parent(clk, parent_clk);
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

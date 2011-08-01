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

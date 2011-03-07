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
#include <linux/err.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <mach/clkdev.h>

static DEFINE_SPINLOCK(clk_lock);
static LIST_HEAD(picoxcell_clks);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->get_rate ? clk->get_rate(clk) : clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->round_rate ? clk->round_rate(clk, rate) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->set_rate ? clk->set_rate(clk, rate) : 0;
}
EXPORT_SYMBOL(clk_set_rate);

int __clk_enable(struct clk *clk)
{
	if (++clk->enable_count > 0) {
		if (clk->enable)
			clk->enable(clk);
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
EXPORT_SYMBOL(clk_enable);

void __clk_disable(struct clk *clk)
{
	if (--clk->enable_count <= 0) {
		if (clk->disable)
			clk->disable(clk);
	}
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

void picoxcell_clk_add(struct clk *clk)
{
	list_add_tail(&clk->head, &picoxcell_clks);
}

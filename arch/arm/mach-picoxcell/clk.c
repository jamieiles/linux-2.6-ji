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
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <mach/clkdev.h>

#include "soc.h"

static struct dentry *clk_debugfs;
static DEFINE_SPINLOCK(clk_lock);
static LIST_HEAD(picoxcell_clks);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->ops && clk->ops->get_rate ? clk->ops->get_rate(clk) :
		clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->ops && clk->ops->round_rate ?
		clk->ops->round_rate(clk, rate) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->ops && clk->ops->set_rate ?
		clk->ops->set_rate(clk, rate) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(clk_set_rate);

int __clk_enable(struct clk *clk)
{
	if (++clk->enable_count > 0) {
		if (clk->ops && clk->ops->enable)
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
EXPORT_SYMBOL(clk_enable);

void __clk_disable(struct clk *clk)
{
	if (--clk->enable_count <= 0) {
		if (clk->ops && clk->ops->disable)
			clk->ops->disable(clk);
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

static ssize_t clk_rate_read(struct file *filp, char __user *buf, size_t size,
			     loff_t *off)
{
	struct clk *c = filp->f_dentry->d_inode->i_private;
	char rate_buf[128];
	size_t len;

	len = snprintf(rate_buf, sizeof(rate_buf) - 1, "%lu\n",
		       clk_get_rate(c));

	return simple_read_from_buffer(buf, size, off, rate_buf, len);
}

static const struct file_operations clk_rate_fops = {
	.read	= clk_rate_read,
};

static void picoxcell_clk_debugfs_add(struct clk *c)
{
	struct dentry *dentry;

	if (!clk_debugfs)
		return;

	dentry = debugfs_create_dir(c->name, clk_debugfs);

	if (!IS_ERR(dentry)) {
		if (c->rate > 0)
			debugfs_create_u32("rate", S_IRUGO, dentry,
					   (u32 *)&c->rate);
		else
			debugfs_create_file("rate", S_IRUGO, dentry, c,
					    &clk_rate_fops);
		debugfs_create_u32("enable_count", S_IRUGO, dentry,
				   (u32 *)&c->enable_count);
	}
}

void picoxcell_clk_add(struct clk *clk)
{
	list_add_tail(&clk->head, &picoxcell_clks);
	picoxcell_clk_debugfs_add(clk);
}

void __init picoxcell_clk_debugfs_init(void)
{
	struct clk *c;

	if (!picoxcell_debugfs)
		return;

	clk_debugfs = debugfs_create_dir("clk", picoxcell_debugfs);
	if (IS_ERR(clk_debugfs))
		return;

	list_for_each_entry(c, &picoxcell_clks, head)
		picoxcell_clk_debugfs_add(c);
}

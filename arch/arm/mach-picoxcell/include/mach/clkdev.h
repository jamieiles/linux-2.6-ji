/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#ifndef __PICOXCELL_CLKDEV_H__
#define __PICOXCELL_CLKDEV_H__

#include <linux/clkdev.h>
#include <linux/fs.h>

struct clk;

struct clk_ops {
	void		    (*enable)(struct clk *clk);
	void		    (*disable)(struct clk *clk);
	int		    (*is_enabled)(struct clk *clk);
	long		    (*round_rate)(struct clk *clk, unsigned long rate);
	int		    (*set_rate)(struct clk *clk, unsigned long rate);
	int		    (*get_rate)(struct clk *clk);
};

struct clk {
	const char	    *name;
	struct clk	    *parent;
	struct list_head    head;
	int		    rate;
	unsigned	    min, max, step; /* min, max and frequency steps for
					       variable rate clocks in KHz. */
	int		    enable_count;
	int		    clk_num;
	struct clk_ops	    *ops;

#ifdef CONFIG_DEBUG_FS
	struct dentry	    *debug;
#endif /* CONFIG_DEBUG_FS */
};

static inline int __clk_get(struct clk *clk)
{
	return 1;
}

static inline void __clk_put(struct clk *clk)
{
}

extern void picoxcell_clk_add(struct clk *clk);
extern int __clk_enable(struct clk *clk);
extern void __clk_disable(struct clk *clk);

/*
 * Declare a new clock with a given rate and ID. All clocks are enabled by
 * default.
 */
#define FIXED_CLK(__name, __rate, __id, __ops)				\
	static struct clk __name ## _clk = {				\
		.name		= #__name,				\
		.rate		= __rate,				\
		.clk_num	= __id,					\
		.enable_count	= 1,					\
		.ops		= (__ops),				\
	}

#define VARIABLE_CLK(__name, __id, __min, __max, __step, __ops)		\
	static struct clk __name ## _clk = {				\
		.name		= #__name,				\
		.clk_num	= __id,					\
		.enable_count	= 1,					\
		.rate		= -1,					\
		.min		= __min,				\
		.max		= __max,				\
		.step		= __step,				\
		.ops		= (__ops),				\
	}

#define CLK_LOOKUP(__dev_id, __con_id, __clk) \
	{ .dev_id = __dev_id, .con_id = __con_id, .clk = __clk }

#endif /* __PICOXCELL_CLKDEV_H__ */

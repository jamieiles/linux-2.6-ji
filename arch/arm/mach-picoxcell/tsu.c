/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt)	"tsu: " fmt

#include <linux/clkdev.h>
#include <linux/module.h>

#include "picoxcell_core.h"

FIXED_CLK(tsu, 0, -1, NULL);
static struct clk_lookup tsu_clk_lookup = CLK_LOOKUP("macb", "tsu", &tsu_clk);
module_param_named(rate, tsu_clk.rate, int, 0);

/*
 * Initialise the TSU source for the board.  The TSU clock is an input to the
 * onchip GEM network device but can run at different rates per board (and
 * some boards support different input clocks configurable by jumpers.  Use
 * the rate specified as the single parameter by default but allow it to be
 * overriden on the command line with the tsu.rate option.
 */
void __init picoxcell_tsu_init(unsigned long tsu_rate)
{
	if (tsu_clk.rate == 0)
		tsu_clk.rate = tsu_rate;

	picoxcell_clk_add(&tsu_clk);
	clkdev_add(&tsu_clk_lookup);

	pr_info("tsu registered with rate %d\n", tsu_clk.rate);
}

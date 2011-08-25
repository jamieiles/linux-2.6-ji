/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/proc-fns.h>

#include <mach/map.h>
#include <mach/picoxcell_soc.h>

#include "common.h"

typedef void (*phys_reset_t)(unsigned long phys);
static void __iomem *wdt_base;

static void picoxcell_soft_reset(int mode, const char *cmd)
{
	phys_reset_t reset = (phys_reset_t)virt_to_phys(cpu_reset);

	picoxcell_enable_clks_for_reset();
	reset(0xffff0000);
}

void (*arch_reset)(int mode, const char *cmd) = picoxcell_soft_reset;

static const struct of_device_id wdt_match_table[] __initconst = {
	{ .compatible = "snps,dw-apb-wdg" },
	{ /* Sentinel */ }
};

#define WDOG_CTRL_REG_OFFSET		0x00
#define		WDOG_CTRL_EN_MASK	(1 << 0)
#define WDOG_TIMEOUT_REG_OFFSET		0x04

static void picoxcell_wdt_reset(int mode, const char *cmd)
{
	/* Set the shortest possible timeout and start the watchdog. */
	writel(0, wdt_base + WDOG_TIMEOUT_REG_OFFSET);
	writel(WDOG_CTRL_EN_MASK, wdt_base + WDOG_CTRL_REG_OFFSET);

	/* Make sure the watchdog has chance to fire. */
	mdelay(500);
}

/*
 * Setup the reset method.  Prefer a watchdog reset so that the CPU and all
 * onchip peripherals get reset.  If the watchdog isn't available then fall back
 * to a CPU only reset.
 */
static int __init picoxcell_init_reset(void)
{
	struct device_node *np = of_find_matching_node(NULL, wdt_match_table);

	if (!np) {
		pr_info("no watchdog, falling back to cpu reset\n");
		return 0;
	}

	wdt_base = of_iomap(np, 0);
	if (!wdt_base) {
		pr_info("no regs for %s, falling back to cpu reset\n",
			np->full_name);
		goto out_put;
	}

	arch_reset = picoxcell_wdt_reset;

out_put:
	of_node_put(np);

	return 0;
}
arch_initcall(picoxcell_init_reset);

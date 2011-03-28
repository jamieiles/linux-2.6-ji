/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "picoxcell_pm: " fmt
#include <linux/init.h>
#include <linux/suspend.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

static void (*picoxcell_platform_pm_enter)(void);
static void (*picoxcell_platform_pm_exit)(void);

static int picoxcell_pm_valid(suspend_state_t state)
{
	/*
	 * We only support standby mode. There is no point in doing anything
	 * for PM_SUSPEND_MEM as we can't power down the core or the memory
	 * interfaces.
	 *
	 * When we enter standby, the only thing we can do is power down some
	 * of the peripherals.
	 */
	return (state == PM_SUSPEND_ON || state == PM_SUSPEND_STANDBY);
}

static void wait_for_event(void)
{
	pr_debug("entering sleep - wait for interrupt\n");
	/* Drain the writebuffer and wait for an interrupt. */
	dsb();
	/*
	 * wfi instruction is only available on SMP v6K so use the cp15
	 * version.
	 */
	asm volatile("mcr   p15, 0, %0, c7, c0, 4\n" : : "r"(0));

}

static int picoxcell_pm_enter(suspend_state_t state)
{
	int err = 0;

	pr_debug("entering suspend state\n");

	switch (state) {
	case PM_SUSPEND_STANDBY:
		if (picoxcell_platform_pm_enter)
			picoxcell_platform_pm_enter();
		wait_for_event();
		if (picoxcell_platform_pm_exit)
			picoxcell_platform_pm_exit();
		break;

	case PM_SUSPEND_ON:
		wait_for_event();
		break;

	default:
		err = -EOPNOTSUPP;
	}

	pr_debug("resumed\n");

	return 0;
}

static struct platform_suspend_ops picoxcell_pm_ops = {
	.valid	    = picoxcell_pm_valid,
	.enter	    = picoxcell_pm_enter,
};

int picoxcell_init_pm(void (*enter_lowpower)(void),
		      void (*exit_lowpower)(void))
{
	picoxcell_platform_pm_enter = enter_lowpower;
	picoxcell_platform_pm_exit = exit_lowpower;
	suspend_set_ops(&picoxcell_pm_ops);

	return 0;
}

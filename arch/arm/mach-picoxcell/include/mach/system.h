/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <mach/io.h>
#include <mach/picoxcell/picoxcell.h>
#include <mach/picoxcell/wdog.h>

static inline void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks
	 */
	cpu_do_idle();
}

static inline void arch_reset(int mode, const char *cmd)
{
	/*
	 * Set the watchdog to expire as soon as possible and reset the
	 * system.
	 */
	__raw_writel(WDOG_CONTROL_REG_WDT_EN_MASK,
	       IO_ADDRESS(PICOXCELL_WDOG_BASE + WDOG_CONTROL_REG_OFFSET));
	__raw_writel(0, IO_ADDRESS(PICOXCELL_WDOG_BASE +
			     WDOG_TIMEOUT_RANGE_REG_OFFSET));

	/* Give it chance to reset. */
	mdelay(500);

	pr_crit("watchdog reset failed - entering infinite loop\n");
}

#endif /* __ASM_ARCH_SYSTEM_H */

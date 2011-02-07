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
#ifndef PICOXCELL_WDOG_H
#define PICOXCELL_WDOG_H

#define WDOG_CONTROL_REG_OFFSET		    0x00
#define WDOG_TIMEOUT_RANGE_REG_OFFSET	    0x04
#define WDOG_CURRENT_COUNT_REG_OFFSET	    0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_INT_STATUS_REG_OFFSET	    0x10
#define WDOG_CLEAR_REG_OFFSET		    0x14

#define WDOG_CONTROL_REG_RESET		    0x00000016
#define WDOG_TIMEOUT_RANGE_REG_RESET	    0x0000000c
#define WDOG_CURRENT_COUNT_REG_RESET	    0x0fffffff
#define WDOG_COUNTER_RESTART_REG_RESET	    0x00000000
#define WDOG_INT_STATUS_REG_RESET	    0x00000000
#define WDOG_CLEAR_REG_RESET		    0x00000000

#define WDOGCONTROLREGWDT_ENIDX		    0
#define WDOGCONTROLREGRMODIDX		    1
#define WDOGCONTROLREGRPLIDX		    2

#define WDOG_CONTROL_REG_WDT_EN_MASK	    (1 << WDOGCONTROLREGWDT_ENIDX)
#define WDOG_CONTROL_REG_RMOD_MASK	    (1 << WDOGCONTROLREGRMODIDX)
#define WDOG_CONTROL_REG_RPL_MASK	    (0x7 << WDOGCONTROLREGRPLIDX)

#endif /* PICOXCELL_WDOG_H */

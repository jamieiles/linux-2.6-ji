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
#ifndef PICOXCELL_TIMER_H
#define PICOXCELL_TIMER_H

/* The spacing between individual timers. */
#define TIMER_SPACING			    0x14

#define TIMER_LOAD_COUNT_REG_OFFSET	    0x00
#define TIMER_CONTROL_REG_OFFSET	    0x08
#define TIMER_EOI_REG_OFFSET		    0x0c

#define TIMERS_EOI_REG_OFFSET		    0xa4

#define TIMER_ENABLE			    0x00000001
#define TIMER_MODE			    0x00000002
#define TIMER_INTERRUPT_MASK		    0x00000004

#define RTCLK_CCV_REG_OFFSET		    0x00
#define RTCLK_SET_REG_OFFSET		    0x08

#endif /* PICOXCELL_TIMER_H */

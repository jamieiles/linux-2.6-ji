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
#ifndef __IRQS_H__
#define __IRQS_H__

/* VIC0 IRQ Indexes */
#define IRQ_VIC0_BASE	    32
#define IRQ_EMAC	    (31 + IRQ_VIC0_BASE)
#define IRQ_NPMUIRQ	    (30 + IRQ_VIC0_BASE)
#define IRQ_NDMAEXTERRIRQ   (29 + IRQ_VIC0_BASE)
#define IRQ_NDMASIRQ	    (28 + IRQ_VIC0_BASE)
#define IRQ_NDMAIRQ	    (27 + IRQ_VIC0_BASE)
#define IRQ_DMAC2	    (26 + IRQ_VIC0_BASE)
#define IRQ_DMAC1	    (25 + IRQ_VIC0_BASE)
#define IRQ_IPSEC	    (24 + IRQ_VIC0_BASE)
#define IRQ_SRTP	    (23 + IRQ_VIC0_BASE)
#define IRQ_AES		    (22 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO8	    (21 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO7	    (20 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO6	    (19 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO5	    (18 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO4	    (17 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO3	    (16 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO2	    (15 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO1	    (14 + IRQ_VIC0_BASE)
#define IRQ_AXI2PICO0	    (13 + IRQ_VIC0_BASE)
#define IRQ_AXI2CFG	    (12 + IRQ_VIC0_BASE)
#define IRQ_WDG		    (11 + IRQ_VIC0_BASE)
#define IRQ_SSI		    (10 + IRQ_VIC0_BASE)
#define IRQ_AXI_RD_ERR	    (9	+ IRQ_VIC0_BASE)
#define IRQ_AXI_WR_ERR	    (8	+ IRQ_VIC0_BASE)
#define IRQ_TIMER3	    (7	+ IRQ_VIC0_BASE)
#define IRQ_TIMER2	    (6	+ IRQ_VIC0_BASE)
#define IRQ_TIMER1	    (5	+ IRQ_VIC0_BASE)
#define IRQ_TIMER0	    (4	+ IRQ_VIC0_BASE)
#define IRQ_COMMTX	    (3	+ IRQ_VIC0_BASE)
#define IRQ_COMMRX	    (2	+ IRQ_VIC0_BASE)
#define IRQ_SWI		    (1	+ IRQ_VIC0_BASE)

/* VIC1 IRQ Indexes */
#define IRQ_VIC1_BASE	    0
#define IRQ_UART1	    (10 + IRQ_VIC1_BASE)
#define IRQ_UART2	    (9 + IRQ_VIC1_BASE)
#define IRQ_RTC		    (8 + IRQ_VIC1_BASE)
#define __IRQ_GPIO7	    (7 + IRQ_VIC1_BASE)
#define __IRQ_GPIO6	    (6 + IRQ_VIC1_BASE)
#define __IRQ_GPIO5	    (5 + IRQ_VIC1_BASE)
#define __IRQ_GPIO4	    (4 + IRQ_VIC1_BASE)
#define __IRQ_GPIO3	    (3 + IRQ_VIC1_BASE)
#define __IRQ_GPIO2	    (2 + IRQ_VIC1_BASE)
#define __IRQ_GPIO1	    (1 + IRQ_VIC1_BASE)
#define __IRQ_GPIO0	    (0 + IRQ_VIC1_BASE)

/*
 * Virtual GPIO interrupts.
 *
 * We want to enable/disable interrupts for the GPIO pins through the GPIO
 * block itself. To do this we install a chained handler. If a user requests
 * one of the __IRQ_GPIOn interrupts then the GPIO block won't get configured.
 * We provide these interrupts below as virtual ones that will configure the
 * GPIO block and enable the source in the VIC.
 */
#define IRQ_GPIO7	    71
#define IRQ_GPIO6	    70
#define IRQ_GPIO5	    69
#define IRQ_GPIO4	    68
#define IRQ_GPIO3	    67
#define IRQ_GPIO2	    66
#define IRQ_GPIO1	    65
#define IRQ_GPIO0	    64

#define NR_IRQS		    72

#define IRQ_PC30XX_BUS_ERR  40
#define IRQ_PC30XX_UART3    59
#define IRQ_PC30XX_NAND	    41

#endif /* __IRQS_H__ */

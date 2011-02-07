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
#include <linux/io.h>
#include <linux/serial_reg.h>

#include <asm/processor.h>

#include <mach/hardware.h>

#define UART_SHIFT  2

static inline void putc(int c)
{
	void __iomem *uart = (void __iomem *)(PICOXCELL_UART1_BASE);

	while (!(__raw_readl(uart + (UART_LSR << UART_SHIFT)) & UART_LSR_THRE))
		barrier();
	__raw_writel(c & 0xFF, uart + (UART_TX << UART_SHIFT));
}

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
	void __iomem *uart = (void __iomem *)(PICOXCELL_UART1_BASE);

	/* Reset and enable the FIFO's. */
	__raw_writel(UART_FCR_ENABLE_FIFO, uart + (UART_FCR << UART_SHIFT));

	/* Wait for the FIFO's to be enabled. */
	while (!(__raw_readl(uart + (UART_FCR << UART_SHIFT)) &
		 UART_FCR_TRIGGER_14))
		cpu_relax();
	/* Enable divisor access, set length to 8 bits. */
	__raw_writel(UART_LCR_DLAB | UART_LCR_WLEN8,
		     uart + (UART_LCR << UART_SHIFT));
	/* Set for 115200 baud. */
	__raw_writel(0x2, uart + (UART_DLL << UART_SHIFT));
	__raw_writel(0x0, uart + (UART_DLM << UART_SHIFT));
	__raw_writel(UART_LCR_WLEN8, uart + (UART_LCR << UART_SHIFT));
}

#define arch_decomp_wdog()

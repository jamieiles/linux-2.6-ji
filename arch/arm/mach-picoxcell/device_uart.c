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
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/io.h>

#include "picoxcell_core.h"

#define UART_USR_REG_OFFSET			0x7C

struct picoxcell_uart_data {
	int	last_lcr;
};

static void picoxcell_serial_out(struct uart_port *p, int offset, int value)
{
	struct picoxcell_uart_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writel(value, p->membase + offset);
}

static unsigned int picoxcell_serial_in(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readl(p->membase + offset);
}

static int picoxcell_serial_handle_irq(struct uart_port *p)
{
	struct picoxcell_uart_data *d = p->private_data;
	unsigned int iir = readl(p->membase + (UART_IIR << p->regshift));

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		(void)readl(p->membase + UART_USR_REG_OFFSET);
		writel(d->last_lcr, p->membase + (UART_LCR << p->regshift));

		return 1;
	}

	return 0;
}

struct platform_device * __init picoxcell_add_uart(unsigned long addr,
						   int irq, int id)
{
	struct plat_serial8250_port pdata[] = {
		{
			.membase	= IO_ADDRESS(addr),
			.mapbase	= addr,
			.irq		= irq,
			.flags		= UPF_BOOT_AUTOCONF,
			.iotype		= UPIO_MEM32,
			.regshift	= 2,
			.uartclk	= PICOXCELL_BASE_BAUD,
			.serial_out	= picoxcell_serial_out,
			.serial_in	= picoxcell_serial_in,
			.handle_irq	= picoxcell_serial_handle_irq,
		},
		{},
	};
	struct resource res[] = {
		{
			.start		= addr,
			.end		= addr + 0xFFFF,
			.flags		= IORESOURCE_MEM,
		},
		{
			.start		= irq,
			.end		= irq,
			.flags		= IORESOURCE_IRQ,
		},
	};
	struct clk *uart_clk = clk_get(NULL, "uart");
	struct platform_device *pdev;
	int err;

	if (IS_ERR(uart_clk))
		return (struct platform_device *)uart_clk;

	err = clk_enable(uart_clk);
	if (err)
		goto out_put_clk;

	pdata[0].private_data = kzalloc(sizeof(struct picoxcell_uart_data),
					GFP_KERNEL);
	if (!pdata[0].private_data) {
		err = -ENOMEM;
		goto out_disable_clk;
	}

	pdata[0].uartclk = clk_get_rate(uart_clk);
	pdev = platform_device_register_resndata(NULL, "serial8250",
		id + PLAT8250_DEV_PLATFORM1, res, ARRAY_SIZE(res), pdata,
		sizeof(pdata));
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		goto out_free_priv;
	}

	return pdev;

out_free_priv:
	kfree(pdata[0].private_data);
out_disable_clk:
	clk_disable(uart_clk);
out_put_clk:
	clk_put(uart_clk);

	return ERR_PTR(err);
}


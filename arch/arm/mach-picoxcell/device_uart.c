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
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <mach/hardware.h>
#include <mach/io.h>

#include "picoxcell_core.h"

#define UART_USR_REG_OFFSET			0x7C

int __init picoxcell_add_uart(unsigned long addr, int irq, int id)
{
	struct plat_serial8250_port pdata[] = {
		{
			.membase	= IO_ADDRESS(addr),
			.mapbase	= addr,
			.irq		= irq,
			.flags		= UPF_BOOT_AUTOCONF,
			.iotype		= UPIO_DWAPB32,
			.regshift	= 2,
			.uartclk	= PICOXCELL_BASE_BAUD,
			.private_data	=
				(void *)(PHYS_TO_IO(PICOXCELL_UART1_BASE +
						    UART_USR_REG_OFFSET)),
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
		return PTR_ERR(uart_clk);

	err = clk_enable(uart_clk);
	if (err)
		goto out_put_clk;

	pdata[0].uartclk = clk_get_rate(uart_clk);
	pdev = platform_device_register_resndata(NULL, "serial8250",
		id + PLAT8250_DEV_PLATFORM1, res, ARRAY_SIZE(res), pdata,
		sizeof(pdata));
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		goto out_disable_clk;
	}

	return 0;

out_disable_clk:
	clk_disable(uart_clk);
out_put_clk:
	clk_put(uart_clk);

	return err;
}


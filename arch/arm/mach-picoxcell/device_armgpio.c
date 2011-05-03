/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/basic_mmio_gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"

#define GPIO_RES(__name, __addr) \
	{ \
		.start = (__addr), \
		.end = (__addr) + 0x3, \
		.flags = IORESOURCE_MEM, \
		.name = #__name, \
	}

int __init picoxcell_add_gpio_port(int port, int ngpio, int base,
				   const char *const *names)
{
	struct resource res[] = {
		GPIO_RES(dat, PICOXCELL_GPIO_BASE + 0x50 + port * 4),
		GPIO_RES(dirout, PICOXCELL_GPIO_BASE + 0x04 + port * 12),
		GPIO_RES(set, PICOXCELL_GPIO_BASE + 0x00 + port * 12),
	};
	struct bgpio_pdata pdata = {
		.base = base,
		.ngpio = ngpio,
		.names = names,
	};
	struct platform_device *pdev = platform_device_register_resndata(NULL,
		"basic-mmio-gpio", port, res, ARRAY_SIZE(res), &pdata,
		sizeof(pdata));

	return IS_ERR(pdev) ? PTR_ERR(pdev) : 0;
}

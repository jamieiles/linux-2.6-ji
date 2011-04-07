/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/err.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"

int __init picoxcell_add_trng(unsigned long addr)
{
	struct resource res = {
		.start		= addr,
		.end		= addr + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	};
	struct platform_device *pdev =
		platform_device_register_simple("picoxcell-trng", -1, &res, 1);

	return pdev ? 0 : PTR_ERR(pdev);
}

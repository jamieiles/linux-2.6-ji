/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"

int __init picoxcell_add_fuse(struct picoxcell_fuse_map *map)
{
	struct resource res = {
		.start		= PICOXCELL_FUSE_BASE,
		.end		= PICOXCELL_FUSE_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	};
	struct platform_device *pdev =
		platform_device_alloc("picoxcell-fuse", -1);
	int err = -ENOMEM;

	if (!pdev)
		return -ENOMEM;

	err = platform_device_add_resources(pdev, &res, 1);
	if (err)
		goto out_free_dev;

	pdev->dev.platform_data = map;
	err = platform_device_add(pdev);
	if (pdev)
		return 0;

out_free_dev:
	platform_device_put(pdev);

	return err;
}


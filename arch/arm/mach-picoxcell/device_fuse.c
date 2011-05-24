/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
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

/**
 * picoxcell_fuse_read() - read a range of fuses.
 *
 * @addr: the byte address of the fuses to read from (offset from the start of
 * the fuses.
 * @buf: the buffer to store the fuse values in.
 * @nr_bytes: the number of bytes to read.
 */
int picoxcell_fuse_read(unsigned long addr, char *buf, size_t nr_bytes)
{
	struct clk *fuse;
	int err = 0;
	size_t n;

	fuse = clk_get_sys("picoxcell-fuse", NULL);
	if (IS_ERR(fuse)) {
		pr_warn("no fuse clk\n");
		err = PTR_ERR(fuse);
		goto out;
	}

	if (clk_enable(fuse)) {
		pr_warn("unable to enable fuse clk\n");
		err = PTR_ERR(fuse);
		goto out_put;
	}

	for (n = 0; n < nr_bytes; ++n)
		buf[n] = readb(IO_ADDRESS(PICOXCELL_FUSE_BASE) + addr + n);

	clk_disable(fuse);

out_put:
	clk_put(fuse);
out:
	return err;
}

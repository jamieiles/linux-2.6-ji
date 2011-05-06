/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/platform_data/denali.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include "picoxcell_core.h"

int __init picoxcell_add_hw_nand(const struct mtd_partition *parts,
				 unsigned int nr_parts)
{
	struct resource res[] = {
		{
			.start		= PC30XX_NAND_BASE,
			.end		= PC30XX_NAND_BASE + 0xFFFF,
			.flags		= IORESOURCE_MEM,
			.name		= "reg",
		},
		{
			.start		= NAND_CS_BASE,
			.end		= NAND_CS_BASE + 0x20,
			.flags		= IORESOURCE_MEM,
			.name		= "mem",
		},
		{
			.start		= IRQ_PC30XX_NAND,
			.end		= IRQ_PC30XX_NAND,
			.flags		= IORESOURCE_IRQ,
		},
	};
	struct platform_device *pdev =
		platform_device_alloc("denali-nand-mmio", -1);
	struct denali_nand_pdata pdata = {
		.parts			= parts,
		.nr_parts		= nr_parts,
		.nr_ecc_bits		= 8,
		.have_hw_ecc_fixup	= true,
	};
	int err = -ENOMEM;

	if (!pdev)
		return -ENOMEM;

	pdev->dev.dma_mask = kmalloc(sizeof(*pdev->dev.dma_mask), GFP_KERNEL);
	if (!pdev->dev.dma_mask)
		goto out_free_dev;
	*pdev->dev.dma_mask = DMA_BIT_MASK(32);
	pdev->dev.coherent_dma_mask = *pdev->dev.dma_mask;

	err = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (err)
		goto out_free_mask;

	err = platform_device_add_data(pdev, &pdata, sizeof(pdata));
	if (err)
		goto out_free_mask;

	err = platform_device_add(pdev);
	if (pdev)
		return 0;

out_free_mask:
	kfree(pdev->dev.dma_mask);
out_free_dev:
	platform_device_put(pdev);

	return err;
}


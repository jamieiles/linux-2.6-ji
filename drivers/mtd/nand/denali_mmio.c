/*
 * NAND Flash Controller Device Driver
 *
 * Copyright © 2011, Picochip.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "denali.h"

struct denali_mmio {
	struct denali_nand_info	denali;
	struct clk		*clk;
};

static void __iomem *request_and_map(struct device *dev,
				     const struct resource *res)
{
	void __iomem *ptr;

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				     "denali-mmio")) {
		dev_err(dev, "unable to request %s\n", res->name);
		return NULL;
	}

	ptr = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!res)
		dev_err(dev, "ioremap_nocache of %s failed!", res->name);

	return ptr;
}

static int __devinit denali_mmio_probe(struct platform_device *pdev)
{
	struct resource *reg, *mem;
	struct denali_mmio *mmio;
	struct denali_nand_info *denali;
	int ret;

	mmio = devm_kzalloc(&pdev->dev, sizeof(*mmio), GFP_KERNEL);
	if (!mmio)
		return -ENOMEM;
	denali = &mmio->denali;

	reg = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mem");
	if (!reg || !mem) {
		dev_err(&pdev->dev, "resources not completely defined\n");
		return -EINVAL;
	}

	denali->platform = MMIO;
	denali->dev = &pdev->dev;
	denali->irq = platform_get_irq(pdev, 0);
	if (denali->irq < 0) {
		dev_err(&pdev->dev, "no irq defined\n");
		return -ENXIO;
	}

	denali->flash_reg = request_and_map(&pdev->dev, reg);
	if (!denali->flash_reg)
		return -ENOMEM;

	denali->flash_mem = request_and_map(&pdev->dev, mem);
	if (!denali->flash_mem)
		return -ENOMEM;

	mmio->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(mmio->clk)) {
		dev_err(&pdev->dev, "no clk available\n");
		return PTR_ERR(mmio->clk);
	}

	ret = clk_enable(mmio->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clk\n");
		goto out_put_clk;
	}

	ret = denali_init(denali);
	if (ret)
		goto out_disable_clk;

	platform_set_drvdata(pdev, mmio);

	return 0;

out_disable_clk:
	clk_disable(mmio->clk);
out_put_clk:
	clk_put(mmio->clk);

	return ret;
}

static int __devexit denali_mmio_remove(struct platform_device *pdev)
{
	struct denali_mmio *mmio = platform_get_drvdata(pdev);

	denali_remove(&mmio->denali);
	clk_disable(mmio->clk);
	clk_put(mmio->clk);

	return 0;
}

static struct platform_driver denali_mmio_driver = {
	.probe		= denali_mmio_probe,
	.remove		= __devexit_p(denali_mmio_remove),
	.driver		= {
		.name	= "denali-nand-mmio",
	},
};

static int __init denali_init_mmio(void)
{
	return platform_driver_register(&denali_mmio_driver);
}
module_init(denali_init_mmio);

static void __exit denali_exit_mmio(void)
{
	platform_driver_unregister(&denali_mmio_driver);
}
module_exit(denali_exit_mmio);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("MMIO driver for Denali NAND controller");

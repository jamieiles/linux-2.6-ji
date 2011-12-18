/*
 * Copyright (c) 2011 Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

struct dwapb_gpio;

struct dwapb_gpio_bank {
	struct bgpio_chip	bgc;
	unsigned int		bank_idx;
	bool			is_registered;
	struct dwapb_gpio	*gpio;
};

struct dwapb_gpio {
	struct device_node	*of_node;
	struct	device		*dev;
	void __iomem		*regs;
	struct dwapb_gpio_bank	*banks;
	unsigned int		nr_banks;
};

static unsigned int dwapb_gpio_nr_banks(struct device_node *of_node)
{
	unsigned int nr_banks = 0;
	struct device_node *np;

	for_each_child_of_node(of_node, np)
		++nr_banks;

	return nr_banks;
}

static int dwapb_gpio_add_bank(struct dwapb_gpio *gpio,
			       struct device_node *bank_np,
			       unsigned int offs)
{
	struct dwapb_gpio_bank *bank;
	u32 bank_idx, ngpio;
	int err;

	if (of_property_read_u32(bank_np, "reg", &bank_idx)) {
		dev_err(gpio->dev, "invalid bank index for %s\n",
			bank_np->full_name);
		return -EINVAL;
	}
	bank = &gpio->banks[offs];
	bank->gpio = gpio;

	if (of_property_read_u32(bank_np, "nr-gpio", &ngpio)) {
		dev_err(gpio->dev, "failed to get number of gpios for %s\n",
			bank_np->full_name);
		return -EINVAL;
	}

	bank->bank_idx = bank_idx;
	err = bgpio_init(&bank->bgc, gpio->dev, 4,
			 gpio->regs + 0x50 + (bank_idx * 0x4),
			 gpio->regs + 0x00 + (bank_idx * 0xc),
			 NULL, gpio->regs + 0x04 + (bank_idx * 0xc), NULL,
			 false);
	if (err) {
		dev_err(gpio->dev, "failed to init gpio chip for %s\n",
			bank_np->full_name);
		return err;
	}

	bank->bgc.gc.ngpio = ngpio;
	bank->bgc.gc.of_node = bank_np;

	err = gpiochip_add(&bank->bgc.gc);
	if (err)
		dev_err(gpio->dev, "failed to register gpiochip for %s\n",
			bank_np->full_name);
	else
		bank->is_registered = true;

	return err;
}

static void dwapb_gpio_unregister(struct dwapb_gpio *gpio)
{
	unsigned int m;

	for (m = 0; m < gpio->nr_banks; ++m)
		if (gpio->banks[m].is_registered)
			WARN_ON(gpiochip_remove(&gpio->banks[m].bgc.gc));
	of_node_put(gpio->of_node);
}

static int __devinit dwapb_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct dwapb_gpio *gpio;
	struct device_node *np;
	int err;
	unsigned int offs = 0;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	gpio->dev = &pdev->dev;

	gpio->nr_banks = dwapb_gpio_nr_banks(pdev->dev.of_node);
	if (!gpio->nr_banks)
		return -EINVAL;
	gpio->banks = devm_kzalloc(&pdev->dev, gpio->nr_banks *
				   sizeof(*gpio->banks), GFP_KERNEL);
	if (!gpio->banks)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get iomem\n");
		return -ENXIO;
	}
	gpio->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!gpio->regs)
		return -ENOMEM;

	gpio->of_node = of_node_get(pdev->dev.of_node);
	for_each_child_of_node(pdev->dev.of_node, np) {
		err = dwapb_gpio_add_bank(gpio, np, offs++);
		if (err)
			goto out_unregister;
	}
	platform_set_drvdata(pdev, gpio);

	return 0;

out_unregister:
	dwapb_gpio_unregister(gpio);

	return err;
}

static const struct of_device_id dwapb_of_match_table[] = {
	{ .compatible = "snps,dw-apb-gpio" },
	{ /* Sentinel */ }
};

static struct platform_driver dwapb_gpio_driver = {
	.driver		= {
		.name	= "gpio-dwapb",
		.owner	= THIS_MODULE,
		.of_match_table = dwapb_of_match_table,
	},
	.probe		= dwapb_gpio_probe,
};

static int __init dwapb_gpio_init(void)
{
	return platform_driver_register(&dwapb_gpio_driver);
}
postcore_initcall(dwapb_gpio_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Synopsys DesignWare APB GPIO driver");

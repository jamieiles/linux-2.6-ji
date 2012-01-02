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
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define INT_EN_REG_OFFS		0x30
#define INT_MASK_REG_OFFS	0x34
#define INT_TYPE_REG_OFFS	0x38
#define INT_POLARITY_REG_OFFS	0x3c
#define INT_STATUS_REG_OFFS	0x40
#define EOI_REG_OFFS		0x4c

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
	struct irq_chip_generic	*irq_gc;
	unsigned long		toggle_edge;
};

static unsigned int dwapb_gpio_nr_banks(struct device_node *of_node)
{
	unsigned int nr_banks = 0;
	struct device_node *np;

	for_each_child_of_node(of_node, np)
		++nr_banks;

	return nr_banks;
}

static int dwapb_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	struct dwapb_gpio_bank *bank = container_of(bgc, struct
						    dwapb_gpio_bank, bgc);
	struct dwapb_gpio *gpio = bank->gpio;

	return irq_domain_to_irq(&gpio->irq_gc->domain, offset);
}

static void dwapb_toggle_trigger(struct dwapb_gpio *gpio, unsigned int offs)
{
	u32 v = readl(gpio->regs + INT_TYPE_REG_OFFS);

	if (gpio_get_value(gpio->banks[0].bgc.gc.base + offs))
		v &= ~BIT(offs);
	else
		v |= BIT(offs);

	writel(v, gpio->regs + INT_TYPE_REG_OFFS);
}

static void dwapb_irq_handler(u32 irq, struct irq_desc *desc)
{
	struct dwapb_gpio *gpio = irq_get_handler_data(irq);
	u32 irq_status = readl(gpio->regs + INT_STATUS_REG_OFFS);

	while (irq_status) {
		int irqoffset = fls(irq_status) - 1;
		int irq = irq_domain_to_irq(&gpio->irq_gc->domain, irqoffset);

		generic_handle_irq(irq);
		irq_status &= ~(1 << irqoffset);

		if (gpio->toggle_edge & BIT(irqoffset))
			dwapb_toggle_trigger(gpio, irqoffset);
	}
}

static void dwapb_irq_enable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = gc->private;

	u32 val = readl(gpio->regs + INT_EN_REG_OFFS);
	val |= 1 << d->hwirq;
	writel(val, gpio->regs + INT_EN_REG_OFFS);
}

static void dwapb_irq_disable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = gc->private;

	u32 val = readl(gpio->regs + INT_EN_REG_OFFS);
	val &= ~(1 << d->hwirq);
	writel(val, gpio->regs + INT_EN_REG_OFFS);
}

static int dwapb_irq_set_type(struct irq_data *d, u32 type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = gc->private;
	int bit = d->hwirq;
	unsigned long level, polarity;

	if (type & ~(IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
		     IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		return -EINVAL;

	level = readl(gpio->regs + INT_TYPE_REG_OFFS);
	polarity = readl(gpio->regs + INT_POLARITY_REG_OFFS);

	gpio->toggle_edge &= ~BIT(bit);
	if (type & IRQ_TYPE_EDGE_BOTH) {
		gpio->toggle_edge |= BIT(bit);
		level |= (1 << bit);
		dwapb_toggle_trigger(gpio, bit);
	} else if (type & IRQ_TYPE_EDGE_RISING) {
		level |= (1 << bit);
		polarity |= (1 << bit);
	} else if (type & IRQ_TYPE_EDGE_FALLING) {
		level |= (1 << bit);
		polarity &= ~(1 << bit);
	} else if (type & IRQ_TYPE_LEVEL_HIGH) {
		level &= ~(1 << bit);
		polarity |= (1 << bit);
	} else if (type & IRQ_TYPE_LEVEL_LOW) {
		level &= ~(1 << bit);
		polarity &= ~(1 << bit);
	}

	writel(level, gpio->regs + INT_TYPE_REG_OFFS);
	writel(polarity, gpio->regs + INT_POLARITY_REG_OFFS);

	return 0;
}

static int dwapb_create_irqchip(struct dwapb_gpio *gpio,
				struct dwapb_gpio_bank *bank,
				unsigned int irq_base)
{
	struct irq_chip_type *ct;

	gpio->irq_gc = irq_alloc_generic_chip("gpio-dwapb", 1, irq_base,
					      gpio->regs, handle_level_irq);
	if (!gpio->irq_gc)
		return -EIO;

	gpio->irq_gc->domain.of_node = of_node_get(bank->bgc.gc.of_node);
	gpio->irq_gc->private = gpio;
	ct = gpio->irq_gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->chip.irq_set_type = dwapb_irq_set_type;
	ct->chip.irq_enable = dwapb_irq_enable;
	ct->chip.irq_disable = dwapb_irq_disable;
	ct->regs.ack = EOI_REG_OFFS;
	ct->regs.mask = INT_MASK_REG_OFFS;
	irq_setup_generic_chip(gpio->irq_gc, IRQ_MSK(bank->bgc.gc.ngpio),
			       IRQ_GC_INIT_NESTED_LOCK, IRQ_NOREQUEST, 0);

	return 0;
}

static int dwapb_configure_irqs(struct dwapb_gpio *gpio,
				struct dwapb_gpio_bank *bank)
{
	unsigned int m, irq, ngpio = bank->bgc.gc.ngpio;
	int irq_base;

	for (m = 0; m < ngpio; ++m) {
		irq = irq_of_parse_and_map(bank->bgc.gc.of_node, m);
		if (!irq && m == 0) {
			dev_warn(gpio->dev, "no irq for bank %s\n",
				 bank->bgc.gc.of_node->full_name);
			return -ENXIO;
		} else if (!irq) {
			break;
		}

		irq_set_chained_handler(irq, dwapb_irq_handler);
		irq_set_handler_data(irq, gpio);
	}
	bank->bgc.gc.to_irq = dwapb_gpio_to_irq;

	irq_base = irq_alloc_descs(-1, 0, ngpio, NUMA_NO_NODE);
	if (irq_base < 0)
		return irq_base;

	if (dwapb_create_irqchip(gpio, bank, irq_base))
		goto out_free_descs;

	return 0;

out_free_descs:
	irq_free_descs(irq_base, ngpio);

	return -EIO;
}

static int dwapb_gpio_add_bank(struct dwapb_gpio *gpio,
			       struct device_node *bank_np)
{
	struct dwapb_gpio_bank *bank;
	u32 bank_idx, ngpio;
	int err;

	if (of_property_read_u32(bank_np, "reg", &bank_idx)) {
		dev_err(gpio->dev, "invalid bank index for %s\n",
			bank_np->full_name);
		return -EINVAL;
	}
	bank = &gpio->banks[bank_idx];
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

	/*
	 * Only bank A can provide interrupts in all configurations of the IP.
	 */
	if (bank_idx == 0 &&
	    of_get_property(bank_np, "interrupt-controller", NULL))
		dwapb_configure_irqs(gpio, bank);

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
		err = dwapb_gpio_add_bank(gpio, np);
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

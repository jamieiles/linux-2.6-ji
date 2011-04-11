/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"

static void __iomem *gpio_irq_base;

#define INT_EN_REG		(gpio_irq_base + GPIO_INT_EN_REG_OFFSET)
#define INT_MASK_REG		(gpio_irq_base + GPIO_INT_MASK_REG_OFFSET)
#define INT_TYPE_REG		(gpio_irq_base + \
				 GPIO_INT_TYPE_LEVEL_REG_OFFSET)
#define INT_POLARITY_REG	(gpio_irq_base + GPIO_INT_POLARITY_REG_OFFSET)
#define INT_STATUS_REG		(gpio_irq_base + GPIO_INT_STATUS_REG_OFFSET)
#define EOI_REG			(gpio_irq_base + GPIO_PORT_A_EOI_REG_OFFSET)

static void armgpio_irq_enable(struct irq_data *d)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_inten = INT_EN_REG;
	unsigned long val;

	val = readl(port_inten);
	val |= (1 << gpio);
	writel(val, port_inten);
}

static void armgpio_irq_disable(struct irq_data *d)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_inten = INT_EN_REG;
	unsigned long val;

	val = readl(port_inten);
	val &= ~(1 << gpio);
	writel(val, port_inten);
}

static void armgpio_irq_mask(struct irq_data *d)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_mask = INT_MASK_REG;
	unsigned long val;

	val = readl(port_mask);
	val |= (1 << gpio);
	writel(val, port_mask);
}

static void armgpio_irq_ack(struct irq_data *d)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_eoi = EOI_REG;
	unsigned long val;

	/* Edge-sensitive */
	val = readl(port_eoi);
	val |= (1 << gpio);
	writel(val, port_eoi);
}

static void armgpio_irq_unmask(struct irq_data *d)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_intmask = INT_MASK_REG;
	unsigned long val;

	val = readl(port_intmask);
	val &= ~(1 << gpio);
	writel(val, port_intmask);
}

static struct irq_chip armgpio_level_irqchip;
static struct irq_chip armgpio_edge_irqchip;

static int armgpio_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	int gpio = irq_to_gpio(d->irq);
	void __iomem *port_inttype_level = INT_TYPE_REG;
	void __iomem *port_int_polarity = INT_POLARITY_REG;
	unsigned long level, polarity;
	void (*handler)(unsigned int irq, struct irq_desc *desc) =
		handle_level_irq;
	struct irq_chip *chip = &armgpio_level_irqchip;

	if (trigger & ~(IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
			IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		return -EINVAL;

	level = readl(port_inttype_level);
	polarity = readl(port_int_polarity);

	if (trigger & IRQ_TYPE_EDGE_RISING) {
		level	    |= (1 << gpio);
		polarity    |= (1 << gpio);
		handler	    = handle_edge_irq;
		chip	    = &armgpio_edge_irqchip;
	} else if (trigger & IRQ_TYPE_EDGE_FALLING) {
		level	    |= (1 << gpio);
		polarity    &= ~(1 << gpio);
		handler	    = handle_edge_irq;
		chip	    = &armgpio_edge_irqchip;
	} else if (trigger & IRQ_TYPE_LEVEL_HIGH) {
		level	    &= ~(1 << gpio);
		polarity    |= (1 << gpio);
	} else if (trigger & IRQ_TYPE_LEVEL_LOW) {
		level	    &= ~(1 << gpio);
		polarity    &= ~(1 << gpio);
	}

	writel(level, port_inttype_level);
	writel(polarity, port_int_polarity);
	__irq_set_chip_handler_name_locked(d->irq, chip, handler, "gpioirq");

	return 0;
}

static struct irq_chip armgpio_level_irqchip = {
	.name		= "armgpio",
	.irq_mask	= armgpio_irq_mask,
	.irq_unmask	= armgpio_irq_unmask,
	.irq_enable	= armgpio_irq_enable,
	.irq_disable	= armgpio_irq_disable,
	.irq_set_type	= armgpio_irq_set_type,
};

static struct irq_chip armgpio_edge_irqchip = {
	.name		= "armgpio",
	.irq_ack	= armgpio_irq_ack,
	.irq_mask	= armgpio_irq_mask,
	.irq_unmask	= armgpio_irq_unmask,
	.irq_enable	= armgpio_irq_enable,
	.irq_disable	= armgpio_irq_disable,
	.irq_set_type	= armgpio_irq_set_type,
};

static void gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	int i;
	struct irq_data *d = irq_desc_get_irq_data(desc);

	/*
	 * Mask and ack the interrupt in the parent interrupt controller
	 * before handling it.
	 */
	d->chip->irq_mask(&desc->irq_data);
	d->chip->irq_ack(&desc->irq_data);
	for (;;) {
		unsigned long status = readl(INT_STATUS_REG);

		if (!status)
			break;
		writel(status, EOI_REG);

		for (i = 0; i < 8; ++i)
			if (status & (1 << i))
				generic_handle_irq(IRQ_GPIO0 + i);
	}
	d->chip->irq_unmask(&desc->irq_data);
}

/*
 * We want to enable/disable interrupts for the GPIO pins through the GPIO
 * block itself. To do this we install a chained handler. If a user requests
 * one of the __IRQ_GPIOn interrupts then the GPIO block won't get configured.
 * We provide these interrupts below as virtual ones that will configure the
 * GPIO block and enable the source in the VIC.
 *
 * The chained handler simply converts from the virtual IRQ handler to the
 * real interrupt source and calls the GPIO IRQ handler.
 */
void __init armgpio_irq_init(void)
{
	int i;

	gpio_irq_base = ioremap(PICOXCELL_GPIO_BASE, SZ_4K);
	if (!gpio_irq_base) {
		pr_warning("failed to initialize ARM GPIO IRQ's\n");
		return;
	}

	writel(0, INT_EN_REG);
	writel(~0, EOI_REG);
	for (i = IRQ_GPIO0; i <= IRQ_GPIO7; ++i)
		irq_set_chip_and_handler(i, &armgpio_level_irqchip,
					 handle_level_irq);

	for (i = __IRQ_GPIO0; i <= __IRQ_GPIO7; ++i) {
		irq_set_chained_handler(i, gpio_irq_handler);
		set_irq_flags(i, IRQF_VALID);
	}
}

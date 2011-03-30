/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "bus_err: " fmt
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <mach/picoxcell/axi2cfg.h>

/*
 * AXI Bus Read / Write Error Handling
 *
 * Some of the peripherals on the AXI bus can generate aborts. For example, a
 * DMAC trying to DMA from the EBI. This isn't supported and will generate an
 * error response. This can't be recovered from so we report the error and
 * panic.
 *
 * Given a bit number in the AXI2Cfg snoop AXI error IRQ post mask register,
 * give the textual name of the operation that generated the error.
 */
static const char **snoop_err_names;
static const char *axi_bus_error_name(int bit)
{
	return snoop_err_names[bit] ?: "<INVALID SNOOP ERROR>";
}

/* AXI Bus write errors */
static irqreturn_t picoxcell_axi_bus_error_interrupt(int irq, void *dev_id)
{
	/*
	 * If we ever get one of these interrupts then we are in big trouble,
	 * they should never happen. The error condition is non recoverable.
	 */
	unsigned long axi_error =
		axi2cfg_readl(AXI2CFG_AXI_ERR_STATE_REG_OFFSET);
	int bit;

	for_each_set_bit(bit, &axi_error, 32) {
		pr_emerg("AXI bus error [%s] detected\n",
			 axi_bus_error_name(bit));
	}
	panic("unable to handle AXI bus error");

	return IRQ_HANDLED;
}

/* Initialise AXI Bus error handling */
static int __init picoxcell_axi_bus_error_probe(struct platform_device *pdev)
{
	int i = 0, err, irq;

	snoop_err_names = pdev->dev.platform_data;
	if (!snoop_err_names) {
		dev_warn(&pdev->dev, "no bus error names\n");
		err = -EINVAL;
		goto out;
	}

	while ((irq = platform_get_irq(pdev, i++)) >= 0) {
		err = request_irq(irq, picoxcell_axi_bus_error_interrupt, 0,
				  "axi_bus_error", NULL);
		if (err) {
			dev_warn(&pdev->dev,
				 "unable to get axi bus error irq %d\n", irq);
			for (i = i - 1; i >= 0; --i)
				free_irq(irq, NULL);
			goto out;
		}
	}

	/* Make sure no AXI errors are masked */
	axi2cfg_writel(AXI2CFG_AXI_ERR_MASK_NONE,
		       AXI2CFG_AXI_ERR_MASK_REG_OFFSET);

	/* Enable interrupts for all AXI Read & Write errors */
	axi2cfg_writel(AXI2CFG_AXI_ERR_ENABLE_ALL,
		       AXI2CFG_AXI_ERR_ENABLE_REG_OFFSET);

out:
	return err;
}

static struct platform_driver picoxcell_bus_err = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "picoxcell-bus-error",
	},
};

static int __init picoxcell_bus_err_init(void)
{
	return platform_driver_probe(&picoxcell_bus_err,
				     picoxcell_axi_bus_error_probe);
}
module_init(picoxcell_bus_err_init);

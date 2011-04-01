/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Platform driver support for Synopsys DesignWare APB timers.
 */
#include <linux/clk.h>
#include <linux/dw_apb_timer.h>
#include <linux/err.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static int dw_apb_event_probe(struct platform_device *pdev, int irq)
{
	int err;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct dw_apb_clock_event_device *dwclk;
	void __iomem *base;
	struct clk *clk;

	if (!request_mem_region(mem->start, resource_size(mem),
				"dw_apb_timer"))
		return -ENOMEM;

	base = ioremap(mem->start, resource_size(mem));
	if (!base) {
		dev_err(&pdev->dev, "failed to remap i/o memory\n");
		err = -ENOMEM;
		goto out_release_mem;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clk\n");
		err = PTR_ERR(clk);
		goto out_unmap;
	}

	err = clk_enable(clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable clk\n");
		goto out_put_clk;
	}

	dwclk = dw_apb_clockevent_init(0, "dw_apb_timer_plat0", 300, base,
				       irq, clk_get_rate(clk));
	if (!dwclk) {
		err = -ENODEV;
		goto out_disable_clk;
	}

	dw_apb_clockevent_register(dwclk);

	return 0;

out_disable_clk:
	clk_disable(clk);
out_put_clk:
	clk_put(clk);
out_unmap:
	iounmap(base);
out_release_mem:
	release_mem_region(mem->start, resource_size(mem));

	return err;
}

static int dw_apb_source_probe(struct platform_device *pdev)
{
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int err;
	void __iomem *base;
	struct clk *clk;
	struct dw_apb_clocksource *dwclk;

	if (!request_mem_region(mem->start, resource_size(mem),
				"dw_apb_timer"))
		return -ENOMEM;

	base = ioremap(mem->start, resource_size(mem));
	if (!base) {
		dev_err(&pdev->dev, "failed to remap i/o memory\n");
		err = -ENOMEM;
		goto out_release_mem;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clk\n");
		err = PTR_ERR(clk);
		goto out_unmap;
	}

	err = clk_enable(clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable clk\n");
		goto out_put_clk;
	}

	dwclk = dw_apb_clocksource_init(300, "dw_apb_plat0", base,
					clk_get_rate(clk));
	if (!dwclk) {
		err = -ENODEV;
		goto out_disable_clk;
	}
	dw_apb_clocksource_start(dwclk);
	dw_apb_clocksource_register(dwclk);

	return 0;

out_disable_clk:
	clk_disable(clk);
out_put_clk:
	clk_put(clk);
out_unmap:
	iounmap(base);
out_release_mem:
	release_mem_region(mem->start, resource_size(mem));

	return err;
}

static int __devinit dw_apb_timer_probe(struct platform_device *pdev)
{
	int irq = platform_get_irq(pdev, 0);

	/*
	 * If the timer has an interrupt defined then we use it as a
	 * clockevents device otherwise we use it as a clocksource device.
	 */
	return irq >= 0 ? dw_apb_event_probe(pdev, irq) :
		dw_apb_source_probe(pdev);
}

static int __devexit dw_apb_timer_remove(struct platform_device *pdev)
{
	return -EBUSY;
}

static struct platform_driver dw_apb_timer_driver = {
	.probe		= dw_apb_timer_probe,
	.remove		= __devexit_p(dw_apb_timer_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "dw_apb_timer",
	},
};
early_platform_init("earlytimer", &dw_apb_timer_driver);


static int __init dw_apb_timers_init(void)
{
	return platform_driver_register(&dw_apb_timer_driver);
}
module_init(dw_apb_timers_init);

static void __exit dw_apb_timers_exit(void)
{
	platform_driver_unregister(&dw_apb_timer_driver);
}
module_exit(dw_apb_timers_exit);

MODULE_AUTHOR("Jamie Iles");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timer driver for Synopsys DesignWare APB timers");

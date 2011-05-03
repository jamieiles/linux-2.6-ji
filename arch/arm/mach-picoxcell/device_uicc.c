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
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/io.h>

#include "mux.h"
#include "picoxcell_core.h"

#define UICC_CLK_EN_MASK	(1 << 3)
#define UICC_DATA_EN_MASK	(1 << 2)
#define UICC_DATA_INVERT_MASK	(1 << 0)

static DEFINE_MUTEX(uicc_cfg_mutex);

static ssize_t uicc_clk_en_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long uicc_cfg;

	if (mutex_lock_interruptible(&uicc_cfg_mutex))
		return -ERESTARTSYS;

	uicc_cfg = axi2cfg_readl(AXI2CFG_UICC_CFG_REG_OFFSET);

	mutex_unlock(&uicc_cfg_mutex);

	return sprintf(buf, "%s\n", uicc_cfg & UICC_CLK_EN_MASK ?
		       "enabled" : "disabled");
}

static ssize_t uicc_clk_en_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	unsigned long uicc_cfg;
	int enable = 0;

	if (sysfs_streq(buf, "enabled"))
		enable = 1;
	else if (!sysfs_streq(buf, "disabled"))
		return -EINVAL;

	if (mutex_lock_interruptible(&uicc_cfg_mutex))
		return -ERESTARTSYS;

	uicc_cfg = axi2cfg_readl(AXI2CFG_UICC_CFG_REG_OFFSET);
	uicc_cfg &= ~UICC_CLK_EN_MASK;
	if (enable)
		uicc_cfg |= UICC_CLK_EN_MASK;
	axi2cfg_writel(uicc_cfg, AXI2CFG_UICC_CFG_REG_OFFSET);

	mutex_unlock(&uicc_cfg_mutex);

	return len;
}
static DEVICE_ATTR(clk_en, 0644, uicc_clk_en_show, uicc_clk_en_store);

int __init picoxcell_add_uicc(unsigned long addr, int irq, int id,
			      bool data_invert)
{
	struct platform_device *pdev;
	int err;
	unsigned long uicc_cfg;
	const struct mux_cfg muxcfg[] = {
		MUXCFG("usim_clk", MUX_PERIPHERAL_USIM),
		MUXCFG("usim_io", MUX_PERIPHERAL_USIM),
	};

	pdev = picoxcell_add_uart(addr, irq, id);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	err = device_create_file(&pdev->dev, &dev_attr_clk_en);
	if (err) {
		dev_err(&pdev->dev, "failed to create clk_en attr\n");
		goto out_put_dev;
	}

	err = mux_configure_table(muxcfg, ARRAY_SIZE(muxcfg));
	if (err) {
		dev_err(&pdev->dev, "failed to configure muxing\n");
		goto out_remove_attr;
	}

	uicc_cfg = axi2cfg_readl(AXI2CFG_UICC_CFG_REG_OFFSET);
	uicc_cfg |= UICC_DATA_EN_MASK;
	uicc_cfg &= ~UICC_DATA_INVERT_MASK;
	if (data_invert)
		uicc_cfg |= UICC_DATA_INVERT_MASK;
	axi2cfg_writel(uicc_cfg, AXI2CFG_UICC_CFG_REG_OFFSET);

	return 0;

out_remove_attr:
	device_remove_file(&pdev->dev, &dev_attr_clk_en);
out_put_dev:
	platform_device_unregister(pdev);

	return err;
}


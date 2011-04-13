/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/pc30xxts.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PC30XXTS_CTRL_REG_OFFSET	0x00
#define PC30XXTS_CTRL_TRIM_MASK		0x0F
#define PC30XXTS_CTRL_WAKE_MASK		(1 << 31)

#define PC30XXTS_SENSE_REG_OFFSET	0x04
#define PC30XXTS_SENSE_TEMP_MASK	0xFFF
#define PC30XXTS_SENSE_VALID_MASK	(1 << 29)
#define PC30XXTS_SENSE_IN_PROGRESS_MASK	(1 << 30)
#define PC30XXTS_SENSE_START_MASK	(1 << 31)

struct pc30xxts_hwmon {
	struct device	*hwmon;
	u8		trim;
	void __iomem	*iobase;
	struct mutex	lock;
};

static ssize_t pc30xxts_show_name(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "pc30xx\n");
}
static DEVICE_ATTR(name, S_IRUGO, pc30xxts_show_name, NULL);

static ssize_t pc30xxts_show_type(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "3\n");
}
static DEVICE_ATTR(temp1_max, S_IRUGO, pc30xxts_show_type, NULL);

static ssize_t pc30xxts_show_max(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "125000\n");
}
static DEVICE_ATTR(temp1_type, S_IRUGO, pc30xxts_show_max, NULL);

static ssize_t pc30xxts_show_min(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "-25000\n");
}
static DEVICE_ATTR(temp1_min, S_IRUGO, pc30xxts_show_min, NULL);

static ssize_t pc30xxts_show_input(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pc30xxts_hwmon *hwmon = dev_get_drvdata(dev);
	int temp;
	unsigned long sense;

	if (mutex_lock_interruptible(&hwmon->lock))
		return -ERESTARTSYS;

	/* Program the TRIM adjustment and wake the sensor. */
	writel(PC30XXTS_CTRL_WAKE_MASK |
	       (hwmon->trim & PC30XXTS_CTRL_TRIM_MASK),
	       hwmon->iobase + PC30XXTS_CTRL_REG_OFFSET);

	/* Sense the temperature. */
	writel(PC30XXTS_SENSE_START_MASK,
	       hwmon->iobase + PC30XXTS_SENSE_REG_OFFSET);
	do {
		sense = readl(hwmon->iobase + PC30XXTS_SENSE_REG_OFFSET);
	} while (!(sense & PC30XXTS_SENSE_VALID_MASK));

	/* Put the sensor back to sleep again. */
	writel(0, hwmon->iobase + PC30XXTS_CTRL_REG_OFFSET);

	mutex_unlock(&hwmon->lock);

	temp = sense & PC30XXTS_SENSE_TEMP_MASK;
	temp = ((temp * 233000) / 4096) - 65500;

	return sprintf(buf, "%d\n", temp);
}
static DEVICE_ATTR(temp1_input, S_IRUGO, pc30xxts_show_input, NULL);

static ssize_t pc30xxts_show_trim(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pc30xxts_hwmon *hwmon = dev_get_drvdata(dev);
	ssize_t ret;

	if (mutex_lock_interruptible(&hwmon->lock))
		return -ERESTARTSYS;
	ret = sprintf(buf, "%u\n", hwmon->trim);
	mutex_unlock(&hwmon->lock);

	return ret;
}

static ssize_t pc30xxts_store_trim(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct pc30xxts_hwmon *hwmon = dev_get_drvdata(dev);
	u8 trim;
	int err;

	if (mutex_lock_interruptible(&hwmon->lock))
		return -ERESTARTSYS;

	err = kstrtou8(buf, 0, &trim);
	if (err)
		goto out;
	hwmon->trim = trim;

out:
	mutex_unlock(&hwmon->lock);

	return err ?: len;
}
static DEVICE_ATTR(temp1_trim, S_IRUGO | S_IWUSR, pc30xxts_show_trim,
		   pc30xxts_store_trim);

static struct attribute *pc30xxts_attributes[] = {
	&dev_attr_name.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_min.attr,
	&dev_attr_temp1_type.attr,
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_trim.attr,
	NULL,
};

static const struct attribute_group pc30xxts_attr_group = {
	.attrs	= pc30xxts_attributes,
};

static int __devinit pc30xxts_probe(struct platform_device *pdev)
{
	struct pc30xxts_hwmon *hwmon;
	struct resource *iomem;
	int err;
	struct pc30xxts_pdata *pdata = pdev->dev.platform_data;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;
	mutex_init(&hwmon->lock);

	if (pdata)
		hwmon->trim = pdata->trim;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(&pdev->dev, "no iomem resource defined\n");
		return -ENOMEM;
	}

	if (!devm_request_mem_region(&pdev->dev, iomem->start,
				     resource_size(iomem),
				     dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "unable to request iomem\n");
		return -ENOMEM;
	}

	hwmon->iobase = devm_ioremap(&pdev->dev, iomem->start,
				     resource_size(iomem));
	if (!hwmon->iobase) {
		dev_err(&pdev->dev, "unable to remap iomem\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &pc30xxts_attr_group);
	if (err) {
		dev_err(&pdev->dev, "failed to create pc30xxts attribute group\n");
		return err;
	}

	hwmon->hwmon = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon->hwmon))
		goto out_remove_group;

	platform_set_drvdata(pdev, hwmon);

	return 0;

out_remove_group:
	sysfs_remove_group(&pdev->dev.kobj, &pc30xxts_attr_group);

	return err;
}

static int __devexit pc30xxts_remove(struct platform_device *pdev)
{
	struct pc30xxts_hwmon *hwmon = platform_get_drvdata(pdev);

	hwmon_device_unregister(hwmon->hwmon);
	sysfs_remove_group(&pdev->dev.kobj, &pc30xxts_attr_group);

	return 0;
}

static struct platform_driver pc30xxts_driver = {
	.probe		= pc30xxts_probe,
	.remove		= __devexit_p(pc30xxts_remove),
	.driver		= {
		.name	= "pc30xxts",
		.owner	= THIS_MODULE,
	},
};

static int __init pc30xxts_init(void)
{
	return platform_driver_register(&pc30xxts_driver);
}
module_init(pc30xxts_init);

static void __exit pc30xxts_exit(void)
{
	platform_driver_unregister(&pc30xxts_driver);
}
module_exit(pc30xxts_exit);

MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Picochip picoxcell pc30xx temperature sensor driver");
MODULE_LICENSE("GPL");

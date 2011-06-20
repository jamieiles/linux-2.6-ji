/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This driver implements an OTP backend for reading and writing the
 * OTP memory in Picochip PC30XX devices. This OTP can be used for executing
 * secure boot code or for the secure storage of keys and any other user data.
 */
#define pr_fmt(fmt) "pc30xxotp: " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/otp.h>
#include <linux/platform_device.h>

/* The control and status registers follow the AXI OTP map. */
#define OTP_CTRL_BASE				0x4000

#define OTP_MACRO_PWDN_REG_OFFSET		0x00
#define		PWDN_EN_MASK			(1 << 0)
#define OTP_MACRO_STATUS_REG_OFFSET		0x04
#define		PGM_FAIL_MASK			(1 << 3)
#define		PGM_JTAG_EN_MASK		(1 << 2)
#define		PGM_IN_PWDN_MASK		(1 << 1)
#define		PGM_BUSY_MASK			(1 << 0)
#define OTP_MACRO_PGM_ADDR_REG_OFFSET		0x28
#define OTP_MACRO_PGM_DATAL_REG_OFFSET		0x30
#define OTP_MACRO_PGM_DATAH_REG_OFFSET		0x34
#define OTP_MACRO_PGM_CMD_REG_OFFSET		0x38
#define		PGM_CMD_START			0x50524f47
#define OTP_MACRO_PGM_STATUS_REG_OFFSET		0x3c
#define		PGM_STATUS_IN_PROGRESS_MASK	(1 << 0)

#define PC30XX_OTP_WORD_SIZE			8

/*
 * The number of words in the OTP device. The device is 16K bytes and the word
 * size is 64 bits.
 */
#define OTP_NUM_WORDS	    (SZ_16K / PC30XX_OTP_WORD_SIZE)

struct pc30xx_otp {
	struct otp_device   *dev;
	void __iomem	    *iomem;
	struct clk	    *clk;
};

static inline void pc30xx_otp_write_reg(struct pc30xx_otp *otp,
					unsigned reg_num, u32 value)
{
	writel(value, otp->iomem + OTP_CTRL_BASE + reg_num);
}

static inline u32 pc30xx_otp_read_reg(struct pc30xx_otp *otp, unsigned reg_num)
{
	return readl(otp->iomem + OTP_CTRL_BASE + reg_num);
}

static int pc30xx_otp_read_word(struct otp_device *otp_dev,
				struct otp_region *region, unsigned long addr,
				u64 *word)
{
	struct pc30xx_otp *otp = otp_dev_get_drvdata(otp_dev);
	void __iomem *byte_addr = addr * PC30XX_OTP_WORD_SIZE + otp->iomem;

	*word = readl(byte_addr);
	*word |= (u64)readl(byte_addr + 4) << 32;

	return 0;
}

#ifdef CONFIG_OTP_WRITE_ENABLE
static int pc30xx_otp_write_word(struct otp_device *otp_dev,
				 struct otp_region *region, unsigned long addr,
				 u64 word)
{
	struct pc30xx_otp *otp = otp_dev_get_drvdata(otp_dev);
	u64 v;
	u32 status;
	int ret = 0;

	ret = pc30xx_otp_read_word(otp_dev, region, addr, &v);
	if (ret)
		return ret;

	/* We can't transition from a 1 to a zero. */
	if (~word & v)
		return -EINVAL;

	/* HW expects byte addresses. */
	pc30xx_otp_write_reg(otp, OTP_MACRO_PGM_ADDR_REG_OFFSET,
			     addr * PC30XX_OTP_WORD_SIZE);

	v |= word;
	pc30xx_otp_write_reg(otp, OTP_MACRO_PGM_DATAL_REG_OFFSET,
			     (u32)(v & 0xffffffff));
	pc30xx_otp_write_reg(otp, OTP_MACRO_PGM_DATAH_REG_OFFSET,
			     (u32)(v >> 32));
	pc30xx_otp_write_reg(otp, OTP_MACRO_PGM_CMD_REG_OFFSET,
			     PGM_CMD_START);

	while (pc30xx_otp_read_reg(otp, OTP_MACRO_PGM_STATUS_REG_OFFSET) &
	       (PGM_STATUS_IN_PROGRESS_MASK))
		cpu_relax();

	status = pc30xx_otp_read_reg(otp, OTP_MACRO_STATUS_REG_OFFSET);
	if (status & PGM_FAIL_MASK) {
		ret = -EIO;
		/* Clear the sticky error bit. */
		pc30xx_otp_write_reg(otp, OTP_MACRO_STATUS_REG_OFFSET,
				     status & ~PGM_FAIL_MASK);
	}

	return ret;
}
#else /* CONFIG_OTP_WRITE_ENABLE */
#define pc30xx_otp_write_word		NULL
#endif /* CONFIG_OTP_WRITE_ENABLE */

/*
 * Find out how big the region is. We have a 16KB device which can be split
 * equally into 1, 2, 4 or 8 regions. If a partition is redundant or
 * differential redundancy then this is 2 bits of storage per data bit so half
 * the size. For differential-redundant redundancy, 1 bit of data takes 4 bits
 * of storage so divide by 4.
 */
static ssize_t pc30xx_otp_region_get_size(struct otp_region *region)
{
	return (ssize_t)SZ_16K;
}

static enum otp_redundancy_fmt
pc30xx_otp_region_get_fmt(struct otp_region *region)
{
	return OTP_REDUNDANCY_FMT_REDUNDANT;
}

static const struct otp_region_ops pc30xx_region_ops = {
	.get_size	= pc30xx_otp_region_get_size,
	.get_fmt	= pc30xx_otp_region_get_fmt,
};

static ssize_t pc30xx_otp_get_nr_regions(struct otp_device *dev)
{
	return 1;
}

static const struct otp_device_ops pc30xx_otp_ops = {
	.name		= "pc30xx",
	.owner		= THIS_MODULE,
	.get_nr_regions	= pc30xx_otp_get_nr_regions,
	.write_word	= pc30xx_otp_write_word,
	.read_word	= pc30xx_otp_read_word,
};

static void pc30xx_otp_reset(struct pc30xx_otp *otp)
{
	pc30xx_otp_write_reg(otp, OTP_MACRO_PWDN_REG_OFFSET, 0);
	while (pc30xx_otp_read_reg(otp, OTP_MACRO_STATUS_REG_OFFSET) &
	       PGM_IN_PWDN_MASK)
		cpu_relax();
}

static int __devinit pc30xx_otp_probe(struct platform_device *pdev)
{
	int err;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct otp_device *otp;
	struct otp_region *region;
	struct pc30xx_otp *pc30xx_dev;

	if (!mem) {
		dev_err(&pdev->dev, "no i/o memory\n");
		return -ENXIO;
	}

	if (!devm_request_mem_region(&pdev->dev, mem->start,
				     resource_size(mem), "otp")) {
		dev_err(&pdev->dev, "unable to request i/o memory\n");
		return -EBUSY;
	}

	pc30xx_dev = devm_kzalloc(&pdev->dev, sizeof(*pc30xx_dev), GFP_KERNEL);
	if (!pc30xx_dev)
		return -ENOMEM;

	pc30xx_dev->iomem = devm_ioremap(&pdev->dev, mem->start,
					 resource_size(mem));
	if (!pc30xx_dev->iomem) {
		err = -ENOMEM;
		goto out;
	}

	pc30xx_dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc30xx_dev->clk)) {
		dev_err(&pdev->dev, "device has no clk\n");
		err = PTR_ERR(pc30xx_dev->clk);
		goto out;
	}
	clk_enable(pc30xx_dev->clk);
	pc30xx_otp_reset(pc30xx_dev);

	otp = otp_device_alloc(&pdev->dev, &pc30xx_otp_ops, SZ_16K, 8, 1, 0);
	if (IS_ERR(otp)) {
		err = PTR_ERR(otp);
		goto out_clk_disable;
	}
	otp_dev_set_drvdata(otp, pc30xx_dev);

	pc30xx_dev->dev = otp;
	platform_set_drvdata(pdev, pc30xx_dev);

	region = otp_region_alloc(otp, &pc30xx_region_ops, 0, "region0");
	if (IS_ERR(region)) {
		err = PTR_ERR(region);
		goto out_unregister;
	}

	return 0;

out_unregister:
	otp_device_unregister(otp);
out_clk_disable:
	pc30xx_otp_write_reg(pc30xx_dev, OTP_MACRO_PWDN_REG_OFFSET,
			     PWDN_EN_MASK);
	clk_disable(pc30xx_dev->clk);
	clk_put(pc30xx_dev->clk);
out:
	return err;
}

static int __devexit pc30xx_otp_remove(struct platform_device *pdev)
{
	struct pc30xx_otp *otp = platform_get_drvdata(pdev);

	pc30xx_otp_write_reg(otp, OTP_MACRO_PWDN_REG_OFFSET, PWDN_EN_MASK);
	otp_device_unregister(otp->dev);
	clk_disable(otp->clk);
	clk_put(otp->clk);

	return 0;
}

#ifdef CONFIG_PM
static int pc30xx_otp_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pc30xx_otp *otp = platform_get_drvdata(pdev);

	pc30xx_otp_write_reg(otp, OTP_MACRO_PWDN_REG_OFFSET, PWDN_EN_MASK);
	clk_disable(otp->clk);

	return 0;
}

static int pc30xx_otp_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pc30xx_otp *otp = platform_get_drvdata(pdev);

	clk_enable(otp->clk);
	pc30xx_otp_reset(otp);

	return 0;
}

static const struct dev_pm_ops pc30xx_otp_pm_ops = {
	.suspend	= pc30xx_otp_suspend,
	.resume		= pc30xx_otp_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver pc30xx_otp_driver = {
	.probe		= pc30xx_otp_probe,
	.remove		= __devexit_p(pc30xx_otp_remove),
	.driver		= {
		.name	= "picoxcell-otp-pc30xx",
#ifdef CONFIG_PM
		.pm	= &pc30xx_otp_pm_ops,
#endif /* CONFIG_PM */
	},
};

static int __init pc30xx_otp_init(void)
{
	return platform_driver_register(&pc30xx_otp_driver);
}
module_init(pc30xx_otp_init);

static void __exit pc30xx_otp_exit(void)
{
	platform_driver_unregister(&pc30xx_otp_driver);
}
module_exit(pc30xx_otp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("OTP memory driver for Picochip pc30xx devices");

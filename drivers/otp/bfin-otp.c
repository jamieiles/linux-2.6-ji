/*
 * Blackfin On-Chip OTP Memory Interface
 *
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/otp.h>
#include <linux/types.h>
#include <mtd/mtd-abi.h>

#include <asm/blackfin.h>
#include <asm/bfrom.h>
#include <asm/uaccess.h>

#define stamp(fmt, args...) pr_debug("%s:%i: " fmt "\n", __func__, __LINE__, ## args)
#define stampit() stamp("here i am")
#define pr_init(fmt, args...) ({ static const __initconst char __fmt[] = fmt; printk(__fmt, ## args); })

#define DRIVER_NAME "bfin-otp"
#define PFX DRIVER_NAME ": "
#define BFIN_OTP_SIZE		(8 * 1024)
#define BFIN_PAGE_SIZE		16
#define BFIN_OTP_WORDS_PER_PAGE	2

static struct otp_device *bfin_otp;

/**
 *	bfin_otp_read - Read OTP pages
 *
 *	All reads must be in half page chunks (half page == 64 bits).
 */
static int bfin_read_word(struct otp_device *otp_dev,
			  struct otp_region *region, unsigned long addr,
			  u64 *word)
{
	int err;
	u32 page, flags, ret;

	stampit();
	page = addr / 2;
	flags = (addr & 0x1) ? OTP_UPPER_HALF : OTP_LOWER_HALF;
	stamp("processing page %i (0x%x:%s)", page, flags,
	      (flags & OTP_UPPER_HALF ? "upper" : "lower"));

	err = bfrom_OtpRead(page, flags, word);
	if (err & OTP_MASTER_ERROR) {
		stamp("error from otp: 0x%x", ret);
		err = -EIO;
	} else
		err = 0;

	return err;
}

#ifdef CONFIG_OTP_WRITE_ENABLE
/**
 *	bfin_otp_init_timing - setup OTP timing parameters
 *
 *	Required before doing any write operation.  Algorithms from HRM.
 */
static u32 bfin_otp_init_timing(void)
{
	u32 tp1, tp2, tp3, timing;

	tp1 = get_sclk() / 1000000;
	tp2 = (2 * get_sclk() / 10000000) << 8;
	tp3 = (0x1401) << 15;
	timing = tp1 | tp2 | tp3;
	if (bfrom_OtpCommand(OTP_INIT, timing))
		return 0;

	return timing;
}

/**
 *	bfin_otp_deinit_timing - set timings to only allow reads
 *
 *	Should be called after all writes are done.
 */
static void bfin_otp_deinit_timing(u32 timing)
{
	/* mask bits [31:15] so that any attempts to write fail */
	bfrom_OtpCommand(OTP_CLOSE, 0);
	bfrom_OtpCommand(OTP_INIT, timing & ~(-1 << 15));
	bfrom_OtpCommand(OTP_CLOSE, 0);
}

/**
 *	bfin_otp_write - write OTP pages
 *
 *	All writes must be in half page chunks (half page == 64 bits).
 */
static int bfin_write_word(struct otp_device *otp_dev,
			   struct otp_region *region, unsigned long addr,
			   u64 content)
{
	int err;
	u32 timing, page, base_flags, flags, ret;

	stampit();
	timing = bfin_otp_init_timing();
	if (timing == 0)
		return -EIO;
	base_flags = OTP_CHECK_FOR_PREV_WRITE;

	page = addr / 2;
	flags = base_flags | (addr & 0x1) ? OTP_UPPER_HALF : OTP_LOWER_HALF;
	stamp("processing page %i (0x%x:%s)", page, flags,
	      (flags & OTP_UPPER_HALF ? "upper" : "lower"));
	ret = bfrom_OtpWrite(page, flags, &content);
	if (ret & OTP_MASTER_ERROR) {
		stamp("error from otp: 0x%x", ret);
		err = -EIO;
	} else
		err = 0;

	bfin_otp_deinit_timing(timing);

	return err;
}

static long bfin_lock_word(struct otp_device *otp_dev,
			   struct otp_region *region, unsigned long addr)
{
	u32 timing;
	int ret = -EIO;

	stampit();

	if (!otp_write_enabled(otp_dev))
		return -EACCES;

	timing = bfin_otp_init_timing();
	if (timing) {
		u32 otp_result = bfrom_OtpWrite(addr, OTP_LOCK, NULL);
		stamp("locking page %lu resulted in 0x%x", addr, otp_result);
		if (!(otp_result & OTP_MASTER_ERROR))
			ret = 0;

		bfin_otp_deinit_timing(timing);
	}

	return ret;
}
#else /* CONFIG_OTP_WRITE_ENABLE */
#define bfin_write_word	NULL
#define bfin_lock_word	NULL
#endif /* CONFIG_OTP_WRITE_ENABLE */

static ssize_t bfin_otp_get_nr_regions(struct otp_device *dev)
{
	return 1;
}

static const struct otp_device_ops bfin_otp_ops = {
	.name		= "bfin-otp",
	.owner		= THIS_MODULE,
	.get_nr_regions	= bfin_otp_get_nr_regions,
	.read_word	= bfin_read_word,
	.write_word	= bfin_write_word,
	.lock_word	= bfin_lock_word,
};

static ssize_t bfin_region_get_size(struct otp_region *region)
{
	return BFIN_OTP_SIZE;
}

static enum otp_redundancy_fmt bfin_region_get_fmt(struct otp_region *region)
{
	return OTP_REDUNDANCY_FMT_ECC;
}

static const struct otp_region_ops bfin_region_ops = {
	.get_size	= bfin_region_get_size,
	.get_fmt	= bfin_region_get_fmt,
};

static int __devinit bfin_otp_probe(struct platform_device *pdev)
{
	struct otp_region *region;

	stampit();

	bfin_otp = otp_device_alloc(&pdev->dev, &bfin_otp_ops, BFIN_OTP_SIZE,
				    8, 1, OTP_CAPS_NO_SUBWORD_WRITE);
	if (IS_ERR(bfin_otp)) {
		pr_init(KERN_ERR PFX "failed to create OTP device\n");
		return PTR_ERR(bfin_otp);
	}

	region = otp_region_alloc(bfin_otp, &bfin_region_ops, 1, "region1");
	if (IS_ERR(region)) {
		otp_device_unregister(bfin_otp);
		return PTR_ERR(region);
	}
	pr_init(KERN_INFO PFX "initialized\n");

	return 0;
}

static int __devexit bfin_otp_remove(struct platform_device *pdev)
{
	stampit();

	otp_device_unregister(bfin_otp);

	return 0;
}

static struct platform_driver bfin_otp_driver = {
	.probe		= bfin_otp_probe,
	.remove		= __devexit_p(bfin_otp_remove),
	.driver.name	= "bfin-otp",
};

/**
 *	bfin_otp_init - Initialize module
 *
 *	Registers the device and notifier handler. Actual device
 *	initialization is handled by bfin_otp_open().
 */
static int __init bfin_otp_init(void)
{
	return platform_driver_register(&bfin_otp_driver);
}

/**
 *	bfin_otp_exit - Deinitialize module
 *
 *	Unregisters the device and notifier handler. Actual device
 *	deinitialization is handled by bfin_otp_close().
 */
static void __exit bfin_otp_exit(void)
{
	platform_driver_unregister(&bfin_otp_driver);
}

module_init(bfin_otp_init);
module_exit(bfin_otp_exit);

MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("Blackfin OTP Memory Interface");
MODULE_LICENSE("GPL");

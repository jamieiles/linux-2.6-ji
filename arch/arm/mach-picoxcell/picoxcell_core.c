/*
 * Copyright (c) 2010-2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/sysdev.h>

#include <asm/hardware/vic.h>
#include <asm/mach-types.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

struct dentry *picoxcell_debugfs;

int picoxcell_is_pc3x2(void)
{
	unsigned long device_id =
		__raw_readl(IO_ADDRESS(PICOXCELL_AXI2CFG_BASE +
				       AXI2CFG_DEVICE_ID_REG_OFFSET));
	switch (device_id) {
	case 0x8003:
	case 0x8007:
		return 1;
	default:
		return 0;
	}
}

int picoxcell_is_pc3x3(void)
{
	unsigned long device_id =
		__raw_readl(IO_ADDRESS(PICOXCELL_AXI2CFG_BASE +
				       AXI2CFG_DEVICE_ID_REG_OFFSET));
	switch (device_id) {
	case 0x20:
	case 0x21:
	case 0x22:
		return 1;
	default:
		return 0;
	}
}

int picoxcell_is_pc30xx(void)
{
	unsigned long device_id =
		__raw_readl(IO_ADDRESS(PICOXCELL_AXI2CFG_BASE +
				       AXI2CFG_DEVICE_ID_REG_OFFSET));
	switch (device_id) {
	case 0x30 ... 0x3F:
		return 1;
	default:
		return 0;
	}
}

const struct picoxcell_soc __init *picoxcell_get_soc(void)
{
	unsigned long device_id =
		__raw_readl(IO_ADDRESS(PICOXCELL_AXI2CFG_BASE +
				       AXI2CFG_DEVICE_ID_REG_OFFSET));
	switch (device_id) {
#ifdef CONFIG_PICOXCELL_PC3X2
	case 0x8003:
	case 0x8007:
		return &pc3x2_soc;
#endif /* CONFIG_PICOXCELL_PC3X2 */

#ifdef CONFIG_PICOXCELL_PC3X3
	case 0x20:
	case 0x21:
	case 0x22:
		return &pc3x3_soc;
#endif /* CONFIG_PICOXCELL_PC3X3 */

#ifdef CONFIG_PICOXCELL_PC30XX
	case 0x30 ... 0x3F:
		return &pc30xx_soc;
#endif /* CONFIG_PICOXCELL_PC30XX */

	default:
		panic("unsupported device type %lx", device_id);
	}
}

void __init picoxcell_init_irq(void)
{
	u32 vic0_resume_sources =
		(1 << (IRQ_AXI2PICO8 & 31)) |
		(1 << (IRQ_EMAC & 31)) |
		(1 << (IRQ_WDG & 31));

	vic_init(IO_ADDRESS(PICOXCELL_VIC0_BASE), 32, 0xFFFFFFFE,
		 vic0_resume_sources);
	vic_init(IO_ADDRESS(PICOXCELL_VIC1_BASE), 0, 0x7FF, 0);
}

static const char *picoxcell_get_partname(void)
{
	unsigned long dev_id = axi2cfg_readl(AXI2CFG_DEVICE_ID_REG_OFFSET);
	const char *part = "<unknown>";

	if (dev_id == 0x8003)
		part = "pc302";
	else if (dev_id == 0x8007)
		part = "pc312";
	else if (dev_id == 0x20)
		part = "pc313";
	else if (dev_id == 0x21)
		part = "pc323";
	else if (dev_id == 0x22)
		part = "pc333";
	else if (dev_id == 0x30)
		part = "pc3008";

	return part;
}

static inline unsigned long picoxcell_get_revision(void)
{
	return axi2cfg_readl(AXI2CFG_REVISION_ID_REG_OFFSET);
}

static void report_chipinfo(void)
{
	const char *part = picoxcell_get_partname();
	unsigned long revision = picoxcell_get_revision();

	pr_info("Picochip picoXcell device: %s revision %lu\n", part, revision);
}

static void picoxcell_debugfs_init(void)
{
	picoxcell_debugfs = debugfs_create_dir("picoxcell", NULL);

	if (IS_ERR(picoxcell_debugfs) &&
	    -ENODEV != PTR_ERR(picoxcell_debugfs)) {
		pr_err("failed to create picoxcell debugfs entry (%ld)\n",
		       PTR_ERR(picoxcell_debugfs));
		picoxcell_debugfs = NULL;
	}
}

void __init picoxcell_init_early(void)
{
	const struct picoxcell_soc *soc = picoxcell_get_soc();

	axi2cfg_init();
	picoxcell_sched_clock_init();
	soc->init_clocks();
}

static struct sysdev_class soc_sysdev_class = {
	.name		= "soc",
};

static struct sys_device soc_sysdev_device = {
	.id		= 0,
	.cls		= &soc_sysdev_class,
};

static ssize_t die_ident_show(struct sys_device *sysdev,
			      struct sysdev_attribute *attr, char *buf)
{
	u8 die_ident[16];
	int err, i;

	err = picoxcell_fuse_read(0x60, die_ident, 16);
	if (err)
		return err;

	for (i = 0; i < 16; ++i)
		sprintf(buf + 2 * i, "%02x", die_ident[i]);
	sprintf(buf + 32, "\n");

	return 33;
}
static SYSDEV_ATTR(die_ident, 0444, die_ident_show, NULL);

static ssize_t revision_show(struct sys_device *sysdev,
			     struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", picoxcell_get_revision());
}
static SYSDEV_ATTR(revision, 0444, revision_show, NULL);

static ssize_t part_show(struct sys_device *sysdev,
			 struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", picoxcell_get_partname());
}
static SYSDEV_ATTR(part, 0444, part_show, NULL);

static ssize_t boot_mode_show(struct sys_device *sysdev,
			      struct sysdev_attribute *attr, char *buf)
{
	unsigned long syscfg = axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET);
	u8 otp_fuses;
	const char *boot_mode;
	const char *otp_boot = "";
	unsigned otp_boot_mode = 0;

	/* We're interested in fuses 1018:1017, and otp_fuses starts at 992. */
	if (picoxcell_fuse_read(127, &otp_fuses, 1))
		return -EIO;
	otp_boot_mode = (otp_fuses >> 1) & 0x3;

	switch (syscfg & 0x3) {
	case 0x0:
		boot_mode = "parallel";
		break;
	case 0x1:
		boot_mode = "ssi";
		break;
	case 0x2:
		boot_mode = "mii";
		break;
	case 0x3:
		boot_mode = "nand";
		break;
	}

	if (!picoxcell_is_pc3x2()) {
		if (0x2 == otp_boot_mode)
			otp_boot = ":otp";
		else if (0x1 == otp_boot_mode)
			boot_mode = "otp";
	}

	return sprintf(buf, "%s%s\n", boot_mode, otp_boot);
}
static SYSDEV_ATTR(boot_mode, 0444, boot_mode_show, NULL);

static ssize_t single_memif_show(struct sys_device *sysdev,
				 struct sysdev_attribute *attr, char *buf)
{
	u8 memif_fuse;
	int err;
	bool single_memif = false;

	/*
	 * Fuse 1001 can be blown to indicate that the ARM memif is not
	 * connected.
	 */
	err = picoxcell_fuse_read(125, &memif_fuse, 1);
	if (err)
		return err;
	if (memif_fuse & (1 << 1))
		single_memif = true;

	/*
	 * Bit 9 in the ID register indicates that the ARM memif has not been
	 * bonded out.
	 */
	if (picoxcell_is_pc30xx() &&
	    (axi2cfg_readl(AXI2CFG_ID_REG_OFFSET) & (1 << 9)))
		single_memif = true;

	return sprintf(buf, "%s\n", single_memif ? "yes" : "no");
}
static SYSDEV_ATTR(single_memif, 0444, single_memif_show, NULL);

static struct __init sysdev_attribute *sysdev_attrs[] = {
	&attr_die_ident,
	&attr_revision,
	&attr_part,
	&attr_boot_mode,
	&attr_single_memif,
};

static void socinfo_init(void)
{
	int err, i;

	err = sysdev_class_register(&soc_sysdev_class);
	if (err) {
		pr_err("failed to register sysdev class\n");
		return;
	}

	err = sysdev_register(&soc_sysdev_device);
	if (err) {
		pr_err("failed to register socinfo device\n");
		goto out_unregister_class;
	}

	for (i = 0; i < ARRAY_SIZE(sysdev_attrs); ++i) {
		err = sysdev_create_file(&soc_sysdev_device, sysdev_attrs[i]);
		if (err) {
			pr_err("unable to add %s attr to sysdev\n",
			       sysdev_attrs[i]->attr.name);
			goto out_remove_attrs;
		}
	}

	return;

out_remove_attrs:
	for (--i; i >= 0; --i)
		sysdev_remove_file(&soc_sysdev_device, sysdev_attrs[i]);
	sysdev_unregister(&soc_sysdev_device);
out_unregister_class:
	sysdev_class_unregister(&soc_sysdev_class);
}

void __init picoxcell_core_init(void)
{
	const struct picoxcell_soc *soc = picoxcell_get_soc();

	report_chipinfo();
	picoxcell_debugfs_init();

	soc->init();
	picoxcell_clk_debugfs_init();

	armgpio_irq_init();
	socinfo_init();
}

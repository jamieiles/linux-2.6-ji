/*
 * Copyright (c) 2010-2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "picoxcell_mux: " fmt

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#include <mach/hardware.h>

#include "mux.h"

static void muxing_sysfs_init(void);
static void picoxcell_muxing_debugfs_init(void);
static LIST_HEAD(mux_defs);

static const char *mux_peripheral_names[NR_MUX_SETTINGS] = {
	[MUX_ARM]		= "armgpio",
	[MUX_SD]		= "sdgpio",
	[MUX_UNMUXED]		= "unmuxed",
	[MUX_PERIPHERAL_FRACN]	= "fracn",
	[MUX_PERIPHERAL_EBI]	= "ebi",
	[MUX_PERIPHERAL_PAI]	= "pai",
	[MUX_PERIPHERAL_DECODE]	= "decode",
	[MUX_PERIPHERAL_SSI]	= "ssi",
	[MUX_PERIPHERAL_MII]	= "mii",
	[MUX_PERIPHERAL_MAXIM]	= "maxim",
};

static int mux_periph_name_to_id(const char *name)
{
	int i;

	for (i = 0; i < NR_MUX_SETTINGS; ++i)
		if (sysfs_streq(mux_peripheral_names[i], name))
			return i;

	return -EINVAL;
}

static const char *mux_periph_id_to_name(enum mux_setting setting)
{
	if (setting < 0 || setting >= NR_MUX_SETTINGS)
		return "<invalid>";

	return mux_peripheral_names[setting];
}

static int __init picoxcell_mux_sys_init(void)
{
	muxing_sysfs_init();
	picoxcell_muxing_debugfs_init();

	return 0;
}
module_init(picoxcell_mux_sys_init);

void picoxcell_mux_register(struct mux_def *defs, int nr_defs)
{
	int i;

	for (i = 0; i < nr_defs; ++i)
		list_add_tail(&defs[i].head, &mux_defs);
}

static enum mux_setting mux_get_config_bus(struct mux_def *def)
{
	u16 data = 0;

	/* Wake the AE up. */
	axi2cfg_config_write(def->caeid, 0xA060, &data, 1);

	/* Read the current mask. */
	if (axi2cfg_config_read(def->caeid, def->caddr, &data, 1) != 1) {
		pr_warn("failed to read the muxing setting\n");
		return -EIO;
	}

	if ((data & def->mask) == def->mask) {
		if (def->armgpio >= 0)
			return MUX_ARM;
		if (def->sdgpio >= 0)
			return MUX_SD;
	}

	return def->periph;
}

static int mux_set_config_bus(struct mux_def *def, enum mux_setting setting)
{
	u16 data = 0;

	if (setting != MUX_ARM && setting != MUX_SD)
		return -EINVAL;

	if ((setting == MUX_ARM && def->armgpio < 0) ||
	    (setting == MUX_SD && def->sdgpio < 0))
		return -EINVAL;

	/* Wake the AE up. */
	axi2cfg_config_write(def->caeid, 0xA060, &data, 1);

	/* Set the new muxing mask. */
	if (axi2cfg_config_read(def->caeid, def->caddr, &data, 1) != 1)
		return -EIO;
	data |= def->mask;
	axi2cfg_config_write(def->caeid, def->caddr, &data, 1);

	return 0;
}

static enum mux_setting mux_get_setting(struct mux_def *def)
{
	unsigned long periph_ctrl, gpio_sel;

	if (def->get_setting)
		return def->get_setting(def);

	if (def->flags & MUX_CONFIG_BUS)
		return mux_get_config_bus(def);

	if (def->periph >= 0) {
		periph_ctrl = axi2cfg_readl(def->periph_reg);

		if (def->flags & MUX_INVERT_PERIPH) {
			if (periph_ctrl & (1 << def->periph_bit))
				return def->periph;
			else if (def->periph_b >= 0)
				return def->periph_b;
		} else {
			if (~periph_ctrl & (1 << def->periph_bit))
				return def->periph;
			else if (def->periph_b >= 0)
				return def->periph_b;
		}
	}

	if (def->armgpio >= 0 && def->sdgpio < 0)
		return MUX_ARM;

	if (def->sdgpio >= 0 && def->armgpio < 0)
		return MUX_SD;

	gpio_sel = axi2cfg_readl(def->gpio_reg_offs);

	return gpio_sel & (1 << def->gpio_reg_bit) ? MUX_ARM : MUX_SD;
}

static int mux_configure(struct mux_def *def, enum mux_setting setting)
{
	unsigned long periph_ctrl;

	if (def->flags & MUX_RO)
		return -EPERM;

	if (def->flags & MUX_CONFIG_BUS)
		return mux_set_config_bus(def, setting);

	if (!((def->armgpio >= 0 && setting == MUX_ARM) ||
	      (def->sdgpio >= 0 && setting == MUX_SD) ||
	      (def->periph >= 0 && setting == def->periph) ||
	      (def->periph_b >= 0 && setting == def->periph_b)))
		return -EINVAL;

	if (def->periph > 0) {
		periph_ctrl = axi2cfg_readl(def->periph_reg);

		if (setting == def->periph) {
			/* Enable the peripheral. */
			if (def->flags & MUX_INVERT_PERIPH)
				periph_ctrl |= (1 << def->periph_bit);
			else
				periph_ctrl &= ~(1 << def->periph_bit);
		} else {
			/* Disable the peripheral. */
			if (def->flags & MUX_INVERT_PERIPH)
				periph_ctrl &= ~(1 << def->periph_bit);
			else
				periph_ctrl |= (1 << def->periph_bit);
		}
		axi2cfg_writel(periph_ctrl, def->periph_reg);

		if (def->periph_b >= 0 && setting == def->periph_b)
			return 0;
	}

	if (setting != def->periph && def->gpio_reg_offs >= 0) {
		unsigned long gpio_sel = axi2cfg_readl(def->gpio_reg_offs);

		if (setting == MUX_SD)
			gpio_sel &= ~(1 << def->gpio_reg_bit);
		else
			gpio_sel |= (1 << def->gpio_reg_bit);

		axi2cfg_writel(gpio_sel, def->gpio_reg_offs);
	}

	return 0;
}

int mux_configure_one(const char *name, enum mux_setting setting)
{
	struct mux_def *def = NULL;

	list_for_each_entry(def, &mux_defs, head)
		if (!strcmp(name, def->name))
			return mux_configure(def, setting);

	return -ENXIO;
}

int mux_configure_table(const struct mux_cfg *cfg,
			unsigned int nr_cfgs)
{
	unsigned int n;
	int ret = 0;

	for (n = 0; n < nr_cfgs; ++n) {
		ret = mux_configure_one(cfg[n].name, cfg[n].setting);
		if (ret)
			break;
	}

	return ret;
}

static const char *pin_setting_name(struct mux_def *pin)
{
	enum mux_setting setting = mux_get_setting(pin);

	return mux_periph_id_to_name(setting);
}

static inline struct mux_def *to_mux_def(struct sysdev_attribute *attr)
{
	return container_of(attr, struct mux_def, attr);
}

ssize_t pin_show(struct sys_device *dev, struct sysdev_attribute *attr,
		 char *buf)
{
	struct mux_def *pin = to_mux_def(attr);

	return snprintf(buf, PAGE_SIZE, "%s\n", pin_setting_name(pin));
}

ssize_t pin_store(struct sys_device *dev, struct sysdev_attribute *attr,
		  const char *buf, size_t len)
{
	ssize_t ret = -EINVAL;
	struct mux_def *def = to_mux_def(attr);
	enum mux_setting setting, new_setting;

	if (sysfs_streq(buf, "sdgpio"))
		setting = MUX_SD;
	else if (sysfs_streq(buf, "armgpio"))
		setting = MUX_ARM;
	else if (def->periph >= 0 && sysfs_streq(buf, "peripheral"))
		setting = def->periph;
	else
		setting = mux_periph_name_to_id(buf);

	ret = mux_configure(def, setting);
	if (ret) {
		pr_warn("failed to configure muxing for %s to %s\n",
			def->name, mux_periph_id_to_name(setting));
		return ret;
	}

	new_setting = mux_get_setting(def);
	if (new_setting != setting) {
		pr_warn("failed to set muxing for %s to %s (got %s)\n",
			def->name, mux_periph_id_to_name(setting),
			mux_periph_id_to_name(new_setting));
		return -EBUSY;
	}

	return len;
}

static struct sysdev_class muxing_class = {
	.name		= "io_muxing",
};

static struct sys_device muxing_device = {
	.id		= 0,
	.cls		= &muxing_class,
};

static void muxing_sysfs_init(void)
{
	int err = sysdev_class_register(&muxing_class);
	struct mux_def *def;

	if (err) {
		pr_err("unable to register sysdev class (%d)\n", err);
		return;
	}

	err = sysdev_register(&muxing_device);
	if (err) {
		pr_err("unable to register sysdev device (%d)\n", err);
		return;
	}

	list_for_each_entry(def, &mux_defs, head) {
		err = sysdev_create_file(&muxing_device, &def->attr);
		if (err)
			WARN("unable to create attr for %s\n", def->name);
	}
}

static ssize_t io_muxing_seq_show(struct seq_file *s, void *v)
{
	struct mux_def *def = v;

	if (def == list_first_entry(&mux_defs, struct mux_def, head))
		seq_printf(s, "%16s%16s%10s%10s\n", "name", "setting",
			   "arm", "sd");

	seq_printf(s, "%16s%16s%10d%10d\n", def->name,
		   pin_setting_name(def), def->armgpio, def->sdgpio);

	return 0;
}

static void *io_muxing_seq_start(struct seq_file *s, loff_t *pos)
{
	if (!pos || *pos > 0)
		return NULL;

	return !list_empty(&mux_defs) ?
		list_first_entry(&mux_defs, struct mux_def, head) : NULL;
}

static void *io_muxing_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct mux_def *def = v;

	(*pos)++;

	return (def->head.next == &mux_defs) ? NULL :
		list_entry(def->head.next, struct mux_def, head);
}

static void io_muxing_seq_stop(struct seq_file *s, void *v)
{
}

static const struct seq_operations io_muxing_seq_ops = {
	.start		= io_muxing_seq_start,
	.next		= io_muxing_seq_next,
	.stop		= io_muxing_seq_stop,
	.show		= io_muxing_seq_show,
};

static int io_muxing_debugfs_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &io_muxing_seq_ops);
}

static const struct file_operations io_muxing_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= io_muxing_debugfs_open,
	.llseek		= seq_lseek,
	.read		= seq_read,
	.release	= seq_release,
};

static void picoxcell_muxing_debugfs_init(void)
{
	/* We only get called if debugfs is enabled and configured. */
	struct dentry *mux_debugfs_file =
		debugfs_create_file("io_muxing", 0444, picoxcell_debugfs, NULL,
				    &io_muxing_debugfs_fops);
	if (IS_ERR(mux_debugfs_file)) {
		pr_err("failed to create io_muxing debugfs entry (%ld)\n",
		       PTR_ERR(mux_debugfs_file));
	}
}

/*
 * Generic driver for memory-mapped GPIO controllers.
 *
 * Copyright 2008 MontaVista Software, Inc.
 * Copyright 2008,2010 Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * ....``.```~~~~````.`.`.`.`.```````'',,,.........`````......`.......
 * ...``                                                         ```````..
 * ..The simplest form of a GPIO controller that the driver supports is``
 *  `.just a single "data" register, where GPIO state can be read and/or `
 *    `,..written. ,,..``~~~~ .....``.`.`.~~.```.`.........``````.```````
 *        `````````
                                    ___
_/~~|___/~|   . ```~~~~~~       ___/___\___     ,~.`.`.`.`````.~~...,,,,...
__________|~$@~~~        %~    /o*o*o*o*o*o\   .. Implementing such a GPIO .
o        `                     ~~~~\___/~~~~    ` controller in FPGA is ,.`
                                                 `....trivial..'~`.```.```
 *                                                    ```````
 *  .```````~~~~`..`.``.``.
 * .  The driver supports  `...       ,..```.`~~~```````````````....````.``,,
 * .   big-endian notation, just`.  .. A bit more sophisticated controllers ,
 *  . register the device with -be`. .with a pair of set/clear-bit registers ,
 *   `.. suffix.  ```~~`````....`.`   . affecting the data register and the .`
 *     ``.`.``...```                  ```.. output pins are also supported.`
 *                        ^^             `````.`````````.,``~``~``~~``````
 *                                                   .                  ^^
 *   ,..`.`.`...````````````......`.`.`.`.`.`..`.`.`..
 * .. The expectation is that in at least some cases .    ,-~~~-,
 *  .this will be used with roll-your-own ASIC/FPGA .`     \   /
 *  .logic in Verilog or VHDL. ~~~`````````..`````~~`       \ /
 *  ..````````......```````````                             \o_
 *                                                           |
 *                              ^^                          / \
 *
 *           ...`````~~`.....``.`..........``````.`.``.```........``.
 *            `  8, 16, 32 and 64 bits registers are supported, and``.
 *            . the number of GPIOs is determined by the width of   ~
 *             .. the registers. ,............```.`.`..`.`.~~~.`.`.`~
 *               `.......````.```
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

static void bgpio_write8(void __iomem *reg, unsigned long data)
{
	writeb(data, reg);
}

static unsigned long bgpio_read8(void __iomem *reg)
{
	return readb(reg);
}

static void bgpio_write16(void __iomem *reg, unsigned long data)
{
	writew(data, reg);
}

static unsigned long bgpio_read16(void __iomem *reg)
{
	return readw(reg);
}

static void bgpio_write32(void __iomem *reg, unsigned long data)
{
	writel(data, reg);
}

static unsigned long bgpio_read32(void __iomem *reg)
{
	return readl(reg);
}

#if BITS_PER_LONG >= 64
static void bgpio_write64(void __iomem *reg, unsigned long data)
{
	writeq(data, reg);
}

static unsigned long bgpio_read64(void __iomem *reg)
{
	return readq(reg);
}
#endif /* BITS_PER_LONG >= 64 */

static unsigned long bgpio_pin2mask(struct bgpio_chip *bgc, unsigned int pin)
{
	return 1 << pin;
}

static unsigned long bgpio_pin2mask_be(struct bgpio_chip *bgc,
				       unsigned int pin)
{
	return 1 << (bgc->bits - 1 - pin);
}

static int bgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return bgc->read_reg(bgc->reg_dat) & bgc->pin2mask(bgc, gpio);
}

static void bgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long mask = bgc->pin2mask(bgc, gpio);
	unsigned long flags;

	spin_lock_irqsave(&bgc->lock, flags);

	if (val)
		bgc->data |= mask;
	else
		bgc->data &= ~mask;

	bgc->write_reg(bgc->reg_dat, bgc->data);

	spin_unlock_irqrestore(&bgc->lock, flags);
}

static void bgpio_set_with_clear(struct gpio_chip *gc, unsigned int gpio,
				 int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long mask = bgc->pin2mask(bgc, gpio);

	if (val)
		bgc->write_reg(bgc->reg_set, mask);
	else
		bgc->write_reg(bgc->reg_clr, mask);
}

static void bgpio_set_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long mask = bgc->pin2mask(bgc, gpio);
	unsigned long flags;

	spin_lock_irqsave(&bgc->lock, flags);

	if (val)
		bgc->data |= mask;
	else
		bgc->data &= ~mask;

	bgc->write_reg(bgc->reg_set, bgc->data);

	spin_unlock_irqrestore(&bgc->lock, flags);
}

static int bgpio_simple_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	return 0;
}

static int bgpio_simple_dir_out(struct gpio_chip *gc, unsigned int gpio,
				int val)
{
	gc->set(gc, gpio, val);

	return 0;
}

static int bgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long flags;

	spin_lock_irqsave(&bgc->lock, flags);

	bgc->dir &= ~bgc->pin2mask(bgc, gpio);
	bgc->write_reg(bgc->reg_dir, bgc->dir);

	spin_unlock_irqrestore(&bgc->lock, flags);

	return 0;
}

static int bgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long flags;

	gc->set(gc, gpio, val);

	spin_lock_irqsave(&bgc->lock, flags);

	bgc->dir |= bgc->pin2mask(bgc, gpio);
	bgc->write_reg(bgc->reg_dir, bgc->dir);

	spin_unlock_irqrestore(&bgc->lock, flags);

	return 0;
}

static int bgpio_dir_in_inv(struct gpio_chip *gc, unsigned int gpio)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long flags;

	spin_lock_irqsave(&bgc->lock, flags);

	bgc->dir |= bgc->pin2mask(bgc, gpio);
	bgc->write_reg(bgc->reg_dir, bgc->dir);

	spin_unlock_irqrestore(&bgc->lock, flags);

	return 0;
}

static int bgpio_dir_out_inv(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long flags;

	gc->set(gc, gpio, val);

	spin_lock_irqsave(&bgc->lock, flags);

	bgc->dir &= ~bgc->pin2mask(bgc, gpio);
	bgc->write_reg(bgc->reg_dir, bgc->dir);

	spin_unlock_irqrestore(&bgc->lock, flags);

	return 0;
}

static int bgpio_setup_accessors(struct device *dev,
				 struct bgpio_chip *bgc,
				 bool be)
{

	switch (bgc->bits) {
	case 8:
		bgc->read_reg	= bgpio_read8;
		bgc->write_reg	= bgpio_write8;
		break;
	case 16:
		bgc->read_reg	= bgpio_read16;
		bgc->write_reg	= bgpio_write16;
		break;
	case 32:
		bgc->read_reg	= bgpio_read32;
		bgc->write_reg	= bgpio_write32;
		break;
#if BITS_PER_LONG >= 64
	case 64:
		bgc->read_reg	= bgpio_read64;
		bgc->write_reg	= bgpio_write64;
		break;
#endif /* BITS_PER_LONG >= 64 */
	default:
		dev_err(dev, "unsupported data width %u bits\n", bgc->bits);
		return -EINVAL;
	}

	bgc->pin2mask = be ? bgpio_pin2mask_be : bgpio_pin2mask;

	return 0;
}

/*
 * Create the device and allocate the resources.  For setting GPIO's there are
 * three supported configurations:
 *
 *	- single input/output register resource (named "dat").
 *	- set/clear pair (named "set" and "clr").
 *	- single output register resource and single input resource ("set" and
 *	dat").
 *
 * For the single output register, this drives a 1 by setting a bit and a zero
 * by clearing a bit.  For the set clr pair, this drives a 1 by setting a bit
 * in the set register and clears it by setting a bit in the clear register.
 * The configuration is detected by which resources are present.
 *
 * For setting the GPIO direction, there are three supported configurations:
 *
 *	- simple bidirection GPIO that requires no configuration.
 *	- an output direction register (named "dirout") where a 1 bit
 *	indicates the GPIO is an output.
 *	- an input direction register (named "dirin") where a 1 bit indicates
 *	the GPIO is an input.
 */
static int bgpio_setup_io(struct bgpio_chip *bgc,
			  void __iomem *dat,
			  void __iomem *set,
			  void __iomem *clr)
{

	bgc->reg_dat = dat;
	if (!bgc->reg_dat)
		return -EINVAL;

	if (set && clr) {
		bgc->reg_set = set;
		bgc->reg_clr = clr;
		bgc->gc.set = bgpio_set_with_clear;
	} else if (set && !clr) {
		bgc->reg_set = set;
		bgc->gc.set = bgpio_set_set;
	} else {
		bgc->gc.set = bgpio_set;
	}

	bgc->gc.get = bgpio_get;

	return 0;
}

static int bgpio_setup_direction(struct bgpio_chip *bgc,
				 void __iomem *dirout,
				 void __iomem *dirin)
{
	if (dirout && dirin) {
		return -EINVAL;
	} else if (dirout) {
		bgc->reg_dir = dirout;
		bgc->gc.direction_output = bgpio_dir_out;
		bgc->gc.direction_input = bgpio_dir_in;
	} else if (dirin) {
		bgc->reg_dir = dirin;
		bgc->gc.direction_output = bgpio_dir_out_inv;
		bgc->gc.direction_input = bgpio_dir_in_inv;
	} else {
		bgc->gc.direction_output = bgpio_simple_dir_out;
		bgc->gc.direction_input = bgpio_simple_dir_in;
	}

	return 0;
}

int __devexit bgpio_remove(struct bgpio_chip *bgc)
{
	int err = gpiochip_remove(&bgc->gc);

	kfree(bgc);

	return err;
}
EXPORT_SYMBOL_GPL(bgpio_remove);

int __devinit bgpio_init(struct bgpio_chip *bgc,
			 struct device *dev,
			 unsigned long sz,
			 void __iomem *dat,
			 void __iomem *set,
			 void __iomem *clr,
			 void __iomem *dirout,
			 void __iomem *dirin,
			 bool big_endian)
{
	int ret;

	if (!is_power_of_2(sz))
		return -EINVAL;

	bgc->bits = sz * 8;
	if (bgc->bits > BITS_PER_LONG)
		return -EINVAL;

	spin_lock_init(&bgc->lock);
	bgc->gc.dev = dev;
	bgc->gc.label = dev_name(dev);
	bgc->gc.base = -1;
	bgc->gc.ngpio = bgc->bits;

	ret = bgpio_setup_io(bgc, dat, set, clr);
	if (ret)
		return ret;

	ret = bgpio_setup_accessors(dev, bgc, big_endian);
	if (ret)
		return ret;

	ret = bgpio_setup_direction(bgc, dirout, dirin);
	if (ret)
		return ret;

	bgc->data = bgc->read_reg(bgc->reg_dat);

	return ret;
}
EXPORT_SYMBOL_GPL(bgpio_init);

#ifdef CONFIG_GPIO_GENERIC_PLATFORM

static void __iomem *bgpio_map(struct platform_device *pdev,
			       const char *name,
			       resource_size_t sane_sz,
			       int *err)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	resource_size_t start;
	resource_size_t sz;
	void __iomem *ret;

	*err = 0;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!r)
		return NULL;

	sz = resource_size(r);
	if (sz != sane_sz) {
		*err = -EINVAL;
		return NULL;
	}

	start = r->start;
	if (!devm_request_mem_region(dev, start, sz, r->name)) {
		*err = -EBUSY;
		return NULL;
	}

	ret = devm_ioremap(dev, start, sz);
	if (!ret) {
		*err = -ENOMEM;
		return NULL;
	}

	return ret;
}

struct bgpio_drvdata {
	struct bgpio_chip	*banks;
	unsigned int		nr_banks;
};

static struct bgpio_drvdata *bgpio_alloc_banks(struct device *dev,
					       unsigned int nr_banks)
{
	struct bgpio_drvdata *banks;

	banks = devm_kzalloc(dev, sizeof(*banks), GFP_KERNEL);
	if (!banks)
		return NULL;

	banks->banks = devm_kzalloc(dev, sizeof(*banks->banks) * nr_banks,
				    GFP_KERNEL);
	if (!banks->banks)
		return NULL;

	return banks;
}

static int bgpio_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	void __iomem *dat;
	void __iomem *set;
	void __iomem *clr;
	void __iomem *dirout;
	void __iomem *dirin;
	unsigned long sz;
	bool be;
	int err;
	struct bgpio_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct bgpio_drvdata *banks = bgpio_alloc_banks(&pdev->dev, 1);

	if (!banks)
		return -ENOMEM;
	platform_set_drvdata(pdev, banks);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dat");
	if (!r)
		return -EINVAL;

	sz = resource_size(r);

	dat = bgpio_map(pdev, "dat", sz, &err);
	if (!dat)
		return err ? err : -EINVAL;

	set = bgpio_map(pdev, "set", sz, &err);
	if (err)
		return err;

	clr = bgpio_map(pdev, "clr", sz, &err);
	if (err)
		return err;

	dirout = bgpio_map(pdev, "dirout", sz, &err);
	if (err)
		return err;

	dirin = bgpio_map(pdev, "dirin", sz, &err);
	if (err)
		return err;

	be = !strcmp(platform_get_device_id(pdev)->name, "basic-mmio-gpio-be");

	banks->nr_banks = 1;
	err = bgpio_init(&banks->banks[0], dev, sz, dat, set, clr, dirout,
			 dirin, be);
	if (err)
		return err;

	if (pdata) {
		banks->banks[0].gc.base = pdata->base;
		if (pdata->ngpio > 0)
			banks->banks[0].gc.ngpio = pdata->ngpio;
	}

	return gpiochip_add(&banks->banks[0].gc);
}

static void bgpio_remove_all_banks(struct platform_device *pdev)
{
	struct bgpio_drvdata *banks = platform_get_drvdata(pdev);
	unsigned int m;

	for (m = 0; m < banks->nr_banks; ++m)
		bgpio_remove(&banks->banks[m]);
}

#ifdef CONFIG_OF
enum gpio_generic_of_reg_type {
	GPIO_GENERIC_REG_DAT,
	GPIO_GENERIC_REG_SET,
	GPIO_GENERIC_REG_CLR,
	GPIO_GENERIC_REG_DIROUT,
	GPIO_GENERIC_REG_DIRIN,
	GPIO_GENERIC_NUM_REG_TYPES
};

static const char *
bgpio_of_reg_prop_names[GPIO_GENERIC_NUM_REG_TYPES] = {
	"regoffset-dat",
	"regoffset-set",
	"regoffset-clr",
	"regoffset-dirout",
	"regoffset-dirin",
};

struct gpio_generic_of_template {
	unsigned long reg_mask;		/*
					 * Bitmask of the registers required
					 * for the given compatible string.
					 */
};

#define TEMPLATE_REG(_name) \
	(1 << (GPIO_GENERIC_REG_ ## _name))

static inline bool template_has_reg(const struct gpio_generic_of_template *t,
				    int type)
{
	return t->reg_mask & (1 << type);
}

static void __iomem *
bgpio_of_get_reg(struct device *dev, struct device_node *np,
		 void __iomem *base, int type,
		 const struct gpio_generic_of_template *template)
{
	u32 offs;
	const char *prop;
	int err;

	if (type >= GPIO_GENERIC_NUM_REG_TYPES)
		return ERR_PTR(-EINVAL);
	prop = bgpio_of_reg_prop_names[type];

	err = of_property_read_u32(np, prop, &offs);
	if (err) {
		if (!template_has_reg(template, type))
			return NULL;
		dev_err(dev, "missing %s property\n", prop);
		return ERR_PTR(-EINVAL);
	}

	if (!template_has_reg(template, type)) {
		dev_err(dev, "%s property invalid for this controller\n",
			prop);
		return ERR_PTR(-EINVAL);
	}

	return base + offs;
}

static int
bgpio_of_add_one_bank(struct platform_device *pdev, struct bgpio_chip *bgc,
		      struct device_node *np, void __iomem *iobase,
		      size_t reg_width_bytes, bool be,
		      const struct gpio_generic_of_template *template)
{
	void __iomem *dat = NULL;
	void __iomem *set = NULL;
	void __iomem *clr = NULL;
	void __iomem *dirout = NULL;
	void __iomem *dirin = NULL;
	u32 val;
	int err;

	dat = bgpio_of_get_reg(&pdev->dev, np, iobase, GPIO_GENERIC_REG_DAT,
			       template);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	set = bgpio_of_get_reg(&pdev->dev, np, iobase, GPIO_GENERIC_REG_SET,
			       template);
	if (IS_ERR(set))
		return PTR_ERR(set);

	clr = bgpio_of_get_reg(&pdev->dev, np, iobase, GPIO_GENERIC_REG_CLR,
			       template);
	if (IS_ERR(clr))
		return PTR_ERR(clr);

	dirout = bgpio_of_get_reg(&pdev->dev, np, iobase,
				  GPIO_GENERIC_REG_DIROUT, template);
	if (IS_ERR(dirout))
		return PTR_ERR(dirout);

	dirin = bgpio_of_get_reg(&pdev->dev, np, iobase, GPIO_GENERIC_REG_DIRIN,
			       template);
	if (IS_ERR(dirin))
		return PTR_ERR(dirin);

	if (of_property_read_u32(np, "gpio-generic,nr-gpio", &val)) {
		dev_err(&pdev->dev, "missing gpio-generic,nr-gpio property\n");
		return -EINVAL;
	}

	err = bgpio_init(bgc, &pdev->dev, reg_width_bytes, dat, set, clr,
			 dirout, dirin, be);
	if (err)
		return err;

	bgc->gc.ngpio = val;
	bgc->gc.of_node = np;

	return gpiochip_add(&bgc->gc);
}

static struct gpio_generic_of_template snps_dw_apb_template = {
	.reg_mask = TEMPLATE_REG(DAT) |
		    TEMPLATE_REG(SET) |
		    TEMPLATE_REG(DIROUT),
};

static const struct of_device_id bgpio_of_id_table[] = {
	{ .compatible = "snps,dw-apb-gpio", .data = &snps_dw_apb_template },
	{},
};
MODULE_DEVICE_TABLE(of, bgpio_of_id_table);

static int bgpio_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *iobase;
	int err = 0;
	u32 val;
	size_t reg_width_bytes;
	bool be;
	int nr_banks = 0;
	struct bgpio_drvdata *banks;
	const struct of_device_id *match;

	match = of_match_node(bgpio_of_id_table, np);

	iobase = of_iomap(np, 0);
	if (!iobase)
		return -EIO;

	if (of_property_read_u32(np, "reg-io-width", &val))
		return -EINVAL;
	reg_width_bytes = val;

	be = of_get_property(np, "gpio-generic,big-endian", NULL) ?
		true : false;

	for_each_child_of_node(pdev->dev.of_node, np)
		++nr_banks;

	banks = bgpio_alloc_banks(&pdev->dev, nr_banks);
	if (!banks)
		return -ENOMEM;
	platform_set_drvdata(pdev, banks);
	banks->nr_banks = 0;

	for_each_child_of_node(pdev->dev.of_node, np) {
		err = bgpio_of_add_one_bank(pdev,
					    &banks->banks[banks->nr_banks],
					    np, iobase, reg_width_bytes, be,
					    match->data);
		if (err)
			goto out_remove;
		++banks->nr_banks;
	}

	return 0;

out_remove:
	bgpio_remove_all_banks(pdev);

	return err;
}
#else /* CONFIG_OF */
static inline int bgpio_of_probe(struct platform_device *pdev)
{
	return -ENODEV;
}

#define bgpio_of_id_table NULL
#endif /* CONFIG_OF */

static int __devinit bgpio_pdev_probe(struct platform_device *pdev)
{
	if (platform_get_device_id(pdev))
		return bgpio_platform_probe(pdev);
	else
		return bgpio_of_probe(pdev);
}

static int __devexit bgpio_pdev_remove(struct platform_device *pdev)
{
	bgpio_remove_all_banks(pdev);

	return 0;
}

static const struct platform_device_id bgpio_id_table[] = {
	{ "basic-mmio-gpio", },
	{ "basic-mmio-gpio-be", },
	{},
};
MODULE_DEVICE_TABLE(platform, bgpio_id_table);

static struct platform_driver bgpio_driver = {
	.driver = {
		.name = "basic-mmio-gpio",
		.of_match_table = bgpio_of_id_table,
	},
	.id_table = bgpio_id_table,
	.probe = bgpio_pdev_probe,
	.remove = __devexit_p(bgpio_pdev_remove),
};

static int __init bgpio_platform_init(void)
{
	return platform_driver_register(&bgpio_driver);
}
module_init(bgpio_platform_init);

static void __exit bgpio_platform_exit(void)
{
	platform_driver_unregister(&bgpio_driver);
}
module_exit(bgpio_platform_exit);

#endif /* CONFIG_GPIO_GENERIC_PLATFORM */

MODULE_DESCRIPTION("Driver for basic memory-mapped GPIO controllers");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");

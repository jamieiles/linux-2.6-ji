/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#undef DEBUG
#define pr_fmt(fmt) "picoxcell_fuse: " fmt
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_data/picoxcell_fuse.h>

#define PICOXCELL_FUSE_PROG_TIME_USEC	20

static int test_mode;
module_param(test_mode, bool, 0600);
MODULE_PARM_DESC(test_mode, "Enable test mode to allow prototyping without actually blowing fuses (0=use hardware, 1=use test mode)");

/*
 * A note on reading fuses: some of the fuses such as the keys and customer
 * partitions have read once per boot bits and these allow each word in that
 * region to be read once. Subsequent reads of the word will return undefined
 * data. So if we do our reading bit by bit to cope with unaligned regions
 * then we may not get valid data. To workaround this without leaking
 * confidential data, when we do the first read of a word, cache that value
 * and reuse it until another word is read. Also, provide a helper -
 * clear_last_word() that we should call after we read a region so that the
 * potentially confidential word is not left hanging around. This means that
 * when reading a region, we can't skip around randomly but that's a fair
 * restriction.
 *
 * Region may be read and written through sysfs. The fuses will be available
 * in the fuses group of the platform device and may be written if the
 * write_enable attribute is set to true. When reading and writing, the value
 * should be formatted as a hexadecimal integer and the LSB's will go into the
 * lowest byte addresses.
 *
 * Once blown, fuses changes do not become visible until power cycle and if
 * they change behaviour of the system, this change will not happen until
 * the next power cycle.  Note: SoC reset through the watchdog timer will
 * *not* resample the fuses.
 */
struct picoxcell_fuse {
	struct device			*dev;
	struct miscdevice		miscdev;
	struct picoxcell_fuse_map	*map;
	struct attribute		**attrs;
	struct attribute_group		attr_group;
	void				*mem;
	void __iomem			*regs;
	struct clk			*clk;
	u32				last_word;
	int				last_word_idx;
	struct mutex			lock;
	bool				write_enable;
};

static struct picoxcell_fuse picoxcell_fuse = {
	.attr_group.name	= "fuses",
};

static int read_fuse_word(int idx)
{
	unsigned word_addr = (idx >> 5) * sizeof(u32);
	int val;

	pm_runtime_get_sync(picoxcell_fuse.dev);

	val = test_mode ? *(u32 *)(picoxcell_fuse.mem + word_addr) :
		readl(picoxcell_fuse.regs + word_addr);

	pm_runtime_put(picoxcell_fuse.dev);

	return val;
}

static int read_fuse(int idx)
{
	int word_idx = idx >> 5, bit = idx & 0x1f;
	u32 val;

	if (word_idx != picoxcell_fuse.last_word_idx) {
		picoxcell_fuse.last_word = read_fuse_word(idx);
		picoxcell_fuse.last_word_idx = word_idx;
	}
	val = picoxcell_fuse.last_word;

	return !!(val & (1 << bit));
}

static void clear_last_word(void)
{
	picoxcell_fuse.last_word_idx = -1;
	picoxcell_fuse.last_word = ~0LU;
}

/*
 * Blow a single fuse. If there is a region protection last time program fuse
 * then wire OR that with the global last time program fuse and only try
 * blowing it if neither are programmed.
 *
 * This simply writes to a kmalloc()'d buffer allowing users to prototype
 * before they actually commit to the efuses.
 */
static int blow_fuse_test_mode(int idx)
{
	u8 *p8 = ((u8 *)picoxcell_fuse.mem) + idx / 8;

	*p8 |= (1 << (idx % 8));

	return 0;
}

#define PICOXCELL_FUSE_CTRL_REG_OFFSET			0x200
#define		PICOXCELL_FUSE_CTRL_WRITE_BUSY		(1 << 0)
#define		PICOXCELL_FUSE_CTRL_VDDQ_OE		(1 << 1)
#define		PICOXCELL_FUSE_CTRL_VDDQ		(1 << 2)
#define PICOXCELL_FUSE_WR_BIT_ADDRESS_REG_OFFSET	0x204
#define PICOXCELL_FUSE_WR_PERFORM_REG_OFFSET		0x208
#define		PICOXCELL_FUSE_WR_PERFORM		0x66757365 /* "fuse" */
#define PICOXCELL_FUSE_WRITE_PAD_EN_REG_OFFSET		0x20c
#define		PICOXCELL_FUSE_WRITE_PAD_EN_VALUE	0x656e626c /* "enbl" */
#define PICOXCELL_FUSE_WRITE_PAD_REG_OFFSET		0x210
#define		PICOXCELL_FUSE_WRITE_PAD_VALUE		0x56444451 /* "VDDQ" */

static int blow_fuse_hardware(int idx)
{
	unsigned long control;

	pm_runtime_get_sync(picoxcell_fuse.dev);

	/*
	 * The fuse macro has a maximum time of 1 second that the VDDQ time
	 * can be applied for. This is long enough to blow all of the fuses
	 * but we don't want to get interrupted for an unknown period of
	 * time...
	 */
	local_irq_disable();

	/* Tell the block which fuse to blow and activate the VDDQ voltage. */
	writel(idx, picoxcell_fuse.regs +
	       PICOXCELL_FUSE_WR_BIT_ADDRESS_REG_OFFSET);
	writel(PICOXCELL_FUSE_WRITE_PAD_EN_VALUE, picoxcell_fuse.regs +
	       PICOXCELL_FUSE_WRITE_PAD_EN_REG_OFFSET);
	writel(PICOXCELL_FUSE_WRITE_PAD_VALUE, picoxcell_fuse.regs +
	       PICOXCELL_FUSE_WRITE_PAD_REG_OFFSET);

	/* Give the external circuitry chance to take effect. */
	udelay(picoxcell_fuse.map->vddq_rise_usec);

	/* Start the fuse blowing process. */
	writel(PICOXCELL_FUSE_WR_PERFORM, picoxcell_fuse.regs +
	       PICOXCELL_FUSE_WR_PERFORM_REG_OFFSET);

	/* Wait for the operation to complete. */
	do {
		control = readl(picoxcell_fuse.regs +
				PICOXCELL_FUSE_CTRL_REG_OFFSET);
	} while (control & PICOXCELL_FUSE_CTRL_WRITE_BUSY);

	/* Disable VDDQ. */
	writel(0, picoxcell_fuse.regs + PICOXCELL_FUSE_WRITE_PAD_REG_OFFSET);
	writel(0, picoxcell_fuse.regs +
	       PICOXCELL_FUSE_WRITE_PAD_EN_REG_OFFSET);
	udelay(picoxcell_fuse.map->vddq_fall_usec);

	local_irq_enable();

	pm_runtime_put(picoxcell_fuse.dev);

	return 0;
}

static int blow_fuse(int idx, int ltp_idx)
{
	int ltp = read_fuse(picoxcell_fuse.map->ltp_fuse);

	if (ltp_idx >= 0)
		ltp |= read_fuse(ltp_idx);

	if (ltp || !picoxcell_fuse.write_enable)
		return -EPERM;

	if (idx < 0 || idx >= picoxcell_fuse.map->nr_fuses) {
		dev_dbg(picoxcell_fuse.dev, "attempt to blow invalid fuse (%d)\n",
			idx);
		return -EINVAL;
	}

	return test_mode ? blow_fuse_test_mode(idx) : blow_fuse_hardware(idx);
}

static const struct picoxcell_fuse_range *find_range(int fuse_idx)
{
	int i;

	for (i = 0; picoxcell_fuse.map->ranges[i].name; ++i)
		if (fuse_idx >= picoxcell_fuse.map->ranges[i].start &&
		    fuse_idx <= picoxcell_fuse.map->ranges[i].end)
			return &picoxcell_fuse.map->ranges[i];

	return NULL;
}

static ssize_t picoxcell_fuse_write(struct file *filp, const char __user *buf,
				    size_t len, loff_t *off)
{
	ssize_t ret = 0;
	int i, j;
	loff_t pos = *off;

	if (pos > picoxcell_fuse.map->nr_fuses / 8)
		return -EINVAL;

	len = min_t(size_t, len, picoxcell_fuse.map->nr_fuses / 8 - pos);

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	if (!picoxcell_fuse.write_enable) {
		ret = -EPERM;
		goto out;
	}

	for (i = 0; i < len; ++i) {
		u8 val = 0;

		if (copy_from_user(&val, buf + i, 1)) {
			ret = -EFAULT;
			goto out;
		}

		for (j = 0; j < 8; ++j) {
			int fuse_idx = (pos + i) * 8 + j;
			const struct picoxcell_fuse_range *range =
				find_range(fuse_idx);

			/*
			 * Fuse maps may be sparse and contain reserved holes.
			 * As some ranges aren't aligned to a byte boundary we
			 * can't treat this as an error so we just skip over
			 * it and make sure we don't blow the reserved fuses.
			 */
			if (!range)
				continue;

			if (!(val & (1 << j)))
				continue;

			ret = blow_fuse(fuse_idx, range->last_time_prog);
			if (ret)
				goto out;
		}

	}

	*off += len;

out:
	clear_last_word();
	mutex_unlock(&picoxcell_fuse.lock);

	return ret ?: len;
}

static ssize_t picoxcell_fuse_read(struct file *filp, char __user *buf,
				   size_t len, loff_t *off)
{
	ssize_t ret = 0;
	int i, j;
	loff_t pos = *off;

	if (pos > picoxcell_fuse.map->nr_fuses / 8)
		return -EINVAL;

	len = min_t(size_t, len, picoxcell_fuse.map->nr_fuses / 8 - pos);

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	for (i = 0; i < len; ++i) {
		u8 val = 0;

		for (j = 0; j < 8; ++j)
			val |= read_fuse((pos + i) * 8 + j) << j;

		if (copy_to_user(buf + i, &val, 1)) {
			ret = -EFAULT;
			goto out;
		}
	}

	*off += len;

out:
	clear_last_word();
	mutex_unlock(&picoxcell_fuse.lock);

	return ret ?: len;
}

static loff_t picoxcell_fuse_llseek(struct file *filp, loff_t offs, int origin)
{
	int ret = 0;
	loff_t end;

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	switch (origin) {
	case SEEK_CUR:
		if (filp->f_pos + offs < 0 ||
		    filp->f_pos + offs >= picoxcell_fuse.map->nr_fuses / 8)
			ret = -EINVAL;
		else
			filp->f_pos += offs;

	case SEEK_SET:
		if (offs < 0 || offs >= picoxcell_fuse.map->nr_fuses / 8)
			ret = -EINVAL;
		else
			filp->f_pos = offs;
		break;

	case SEEK_END:
		end = picoxcell_fuse.map->nr_fuses / 8 - 1;
		if (end + offs < 0 || end + offs >= end)
			ret = -EINVAL;
		else
			filp->f_pos = end + offs;
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&picoxcell_fuse.lock);

	return ret ?: filp->f_pos;
}

/*
 * Check that we have a valid value to program. By valid value, we expect
 * that the string is a hexadecimal number, prefixed with '0x', there are no
 * non-whitespace characters after the end and that the value does not occupy
 * more bits than there are in the region.
 */
static bool value_is_valid(const char *value, int start, int end)
{
	const char *p;
	int bits = 0;

	if (value[0] != '0' || value[1] != 'x')
		return false;

	p = value + strlen(value) - 1;
	while (isspace(*p) && p >= value)
		--p;

	for (; p >= value + 2; --p) {
		int v = hex_to_bin(*p);

		if (v < 0)
			return false;

		if (p != value + 2) {
			bits += 4;
		} else {
			if (v & (1 << 3))
				bits += 4;
			else if (v & (1 << 2))
				bits += 3;
			else if (v & (1 << 1))
				bits += 2;
			else if (v & (1 << 0))
				bits += 1;
		}
	}

	return bits > end - start + 1 ? false : true;
}

static inline struct picoxcell_fuse_range *
to_picoxcell_fuse_range(struct device_attribute *attr)
{
	return attr ? container_of(attr, struct picoxcell_fuse_range, attr) :
		NULL;
}

static ssize_t picoxcell_fuse_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct picoxcell_fuse_range *range = to_picoxcell_fuse_range(attr);
	int i, j, offs = (range->end - range->start) % 32;
	u32 v;
	ssize_t ret;

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	/*
	 * Dump the value of a fuse range. Some fuses aren't aligned to a byte
	 * boundary and may not be a multiple of 8 bits. For simplicity (and
	 * the fact that this doesn't need to be lightning fast), just shift
	 * the bits out one by one and output byte by byte.
	 *
	 * Start off by getting so that we can print the rest as 32 bit blocks.
	 */
	ret = sprintf(buf, "0x");
	for (v = 0, i = range->end; i >= range->end - offs; --i) {
		v <<= 1;
		v |= read_fuse(i);
	}
	ret += sprintf(buf + ret, "%x", v);

	for (; i >= range->start; i -= 32) {
		v = 0;
		for (j = i; j > i - 32; --j) {
			v <<= 1;
			v |= read_fuse(j);
		}
		ret += sprintf(buf + ret, "%08x", v);
	}
	ret += sprintf(buf + ret, "\n");

	clear_last_word();
	mutex_unlock(&picoxcell_fuse.lock);

	return ret;
}

static ssize_t picoxcell_fuse_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct picoxcell_fuse_range *range = to_picoxcell_fuse_range(attr);
	const char *p;
	int idx, i, err = 0;

	if (!value_is_valid(buf, range->start, range->end))
		return -EINVAL;

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	/*
	 * Skip any whitespace and newlines after the value we're interested
	 * in.
	 */
	p = buf + strlen(buf) - 1;
	while (p >= buf && isspace(*p))
		--p;

	for (idx = range->start; p >= buf + 2; --p, idx += 4) {
		int v = hex_to_bin(*p);

		for (i = 0; i < 4; ++i) {
			if (!(v & (1 << i)))
				continue;

			err = blow_fuse(i + idx, range->last_time_prog);
			if (err)
				goto out;
		}
	}

out:
	mutex_unlock(&picoxcell_fuse.lock);

	return err ?: len;
}

/*
 * Show the estimated VDDQ active time in microseconds.  This is an estimate
 * as due to the read-once-per-boot protection we can't reliably tell how many
 * fuses have actually been blown.  Instead we provide the worst case where
 * every fuse has been blown.
 */
static ssize_t vddq_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "%d\n", picoxcell_fuse.map->nr_fuses *
		       (PICOXCELL_FUSE_PROG_TIME_USEC +
			picoxcell_fuse.map->vddq_rise_usec +
			picoxcell_fuse.map->vddq_fall_usec));
}
static DEVICE_ATTR(vddq_time_usec, 0400, vddq_show, NULL);

static ssize_t write_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", picoxcell_fuse.write_enable ? "enabled" :
		       "disabled");
}

static ssize_t write_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	int err = 0;

	if (mutex_lock_interruptible(&picoxcell_fuse.lock))
		return -ERESTARTSYS;

	if (sysfs_streq(buf, "enabled"))
		picoxcell_fuse.write_enable = true;
	else if (sysfs_streq(buf, "disabled"))
		picoxcell_fuse.write_enable = false;
	else
		err = -EINVAL;

	mutex_unlock(&picoxcell_fuse.lock);

	return err ?: len;
}
static DEVICE_ATTR(write_enable, 0600, write_enable_show, write_enable_store);

static const struct attribute *control_attrs[] = {
	&dev_attr_vddq_time_usec.attr,
	&dev_attr_write_enable.attr,
	NULL,
};

static void picoxcell_fuse_attrs_free(struct device *dev)
{
	struct attribute **attrs = picoxcell_fuse.attrs;

	sysfs_remove_files(&dev->kobj, control_attrs);
	sysfs_remove_group(&dev->kobj, &picoxcell_fuse.attr_group);

	kfree(attrs);
}

static int picoxcell_fuse_attrs_create(struct device *dev)
{
	int i, nr_ranges = 0, err = -ENOMEM;
	struct picoxcell_fuse_map *map = picoxcell_fuse.map;

	for (i = 0; picoxcell_fuse.map->ranges[i].name; ++i)
		++nr_ranges;

	picoxcell_fuse.attr_group.attrs =
		kzalloc(sizeof(*picoxcell_fuse.attrs) *
			(nr_ranges + 1), GFP_KERNEL);
	if (!picoxcell_fuse.attr_group.attrs)
		goto out;

	for (i = 0; i < nr_ranges; ++i) {
		map->ranges[i].attr = (struct device_attribute) {
			.attr.name	= picoxcell_fuse.map->ranges[i].name,
			.attr.mode	= 0600,
			.show		= picoxcell_fuse_show,
			.store		= picoxcell_fuse_store,
		};
		picoxcell_fuse.attr_group.attrs[i] = &map->ranges[i].attr.attr;
	}

	err = sysfs_create_group(&dev->kobj, &picoxcell_fuse.attr_group);
	if (err)
		goto out_free_attrs;

	err = sysfs_create_files(&dev->kobj, control_attrs);
	if (err)
		goto out_free_attrs;

	goto out;

out_free_attrs:
	picoxcell_fuse_attrs_free(dev);
out:
	return err;
}

static int picoxcell_fuse_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int picoxcell_fuse_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations picoxcell_fuse_fops = {
	.open		= picoxcell_fuse_open,
	.release	= picoxcell_fuse_release,
	.write		= picoxcell_fuse_write,
	.read		= picoxcell_fuse_read,
	.llseek		= picoxcell_fuse_llseek,
};

static struct miscdevice picoxcell_fuse_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "picoxcell_fuse",
	.fops		= &picoxcell_fuse_fops,
};

static int picoxcell_fuse_test_mode_init(void)
{
	picoxcell_fuse.mem = kzalloc(picoxcell_fuse.map->nr_fuses / 8,
				     GFP_KERNEL);
	return picoxcell_fuse.mem ? 0 : -ENOMEM;
}

static void picoxcell_fuse_test_mode_cleanup(void)
{
	kfree(picoxcell_fuse.mem);
}

static int picoxcell_fuse_hardware_init(struct platform_device *pdev)
{
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!iomem) {
		dev_warn(&pdev->dev, "platform device has no io memory\n");
		return -ENOENT;
	}

	if (!devm_request_region(&pdev->dev, iomem->start,
				 resource_size(iomem), "picoxcell_fuse")) {
		dev_warn(&pdev->dev, "no io memory\n");
		return -ENOENT;
	}

	picoxcell_fuse.regs = devm_ioremap(&pdev->dev, iomem->start,
					   resource_size(iomem));
	if (!picoxcell_fuse.regs) {
		dev_warn(&pdev->dev, "unable to remap io memory\n");
		return -ENOMEM;
	}

	picoxcell_fuse.clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(picoxcell_fuse.clk)) {
		dev_warn(&pdev->dev, "no clk!\n");
		return PTR_ERR(picoxcell_fuse.clk);
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	return 0;
}

static void picoxcell_fuse_hardware_cleanup(struct platform_device *pdev)
{
	clk_put(picoxcell_fuse.clk);
}

static int __devinit picoxcell_fuse_probe(struct platform_device *pdev)
{
	int err = -EINVAL;
	struct picoxcell_fuse_map *map = pdev->dev.platform_data;

	mutex_init(&picoxcell_fuse.lock);
	picoxcell_fuse.last_word_idx = -1;
	picoxcell_fuse.map = map;
	picoxcell_fuse.write_enable = false;

	if (!picoxcell_fuse.map) {
		dev_err(&pdev->dev, "no fuse map supplied\n");
		return -EINVAL;
	}

	if (map->nr_fuses * (map->vddq_rise_usec + map->vddq_fall_usec +
			     PICOXCELL_FUSE_PROG_TIME_USEC) > NSEC_PER_SEC) {
		dev_err(&pdev->dev, "VDDQ rise and fall time too large to allow all fuses to be blown.\n");
		return -EINVAL;
	}

	err = picoxcell_fuse_test_mode_init();
	if (err)
		goto out;

	err = picoxcell_fuse_hardware_init(pdev);
	if (err)
		goto out_unmap_test_mode;

	err = misc_register(&picoxcell_fuse_miscdev);
	if (err)
		goto out_unmap_hardware;

	err = picoxcell_fuse_attrs_create(&pdev->dev);
	if (err)
		goto out_unregister;
	picoxcell_fuse.dev = &pdev->dev;
	goto out;

out_unregister:
	misc_deregister(&picoxcell_fuse_miscdev);
out_unmap_hardware:
	picoxcell_fuse_hardware_cleanup(pdev);
out_unmap_test_mode:
	picoxcell_fuse_test_mode_cleanup();
out:
	return err;
}

static int __devexit picoxcell_fuse_remove(struct platform_device *pdev)
{
	picoxcell_fuse_attrs_free(&pdev->dev);
	misc_deregister(&picoxcell_fuse_miscdev);
	picoxcell_fuse_test_mode_cleanup();
	picoxcell_fuse_hardware_cleanup(pdev);

	return 0;
}

static int picoxcell_fuse_suspend(struct device *dev)
{
	clk_disable(picoxcell_fuse.clk);

	return 0;
}

static int picoxcell_fuse_resume(struct device *dev)
{
	return clk_enable(picoxcell_fuse.clk);
}

static const struct dev_pm_ops picoxcell_fuse_pm_ops = {
	.suspend		= picoxcell_fuse_suspend,
	.resume			= picoxcell_fuse_resume,
	.runtime_suspend	= picoxcell_fuse_suspend,
	.runtime_resume		= picoxcell_fuse_resume,
};

static struct platform_driver picoxcell_driver = {
	.probe			= picoxcell_fuse_probe,
	.remove			= __devexit_p(picoxcell_fuse_remove),
	.driver			= {
		.name		= "picoxcell-fuse",
		.pm		= &picoxcell_fuse_pm_ops,
	},
};

static int __init picoxcell_fuse_init(void)
{
	return platform_driver_register(&picoxcell_driver);
}

static void picoxcell_fuse_exit(void)
{
	platform_driver_unregister(&picoxcell_driver);
}

module_init(picoxcell_fuse_init);
module_exit(picoxcell_fuse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Picochip picoXcell fuse block driver");

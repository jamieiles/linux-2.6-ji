/*
 * Copyright 2008-2011 Picochip, All Rights Reserved.
 * http://www.picochip.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "picogpio: " fmt

#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/picogpio_ioctl.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>

static int picogpio_open(struct inode *inode, struct file *filp);

static int picogpio_release(struct inode *inode, struct file *filp);

static long picogpio_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg);

static const struct file_operations picogpio_fops = {
	.open		= picogpio_open,
	.release	= picogpio_release,
	.unlocked_ioctl	= picogpio_ioctl,
};

static struct miscdevice picogpio_dev = {
	.minor	    = MISC_DYNAMIC_MINOR,
	.name	    = "gpio",
	.fops	    = &picogpio_fops,
};

struct picogpio_session {
	struct list_head    list;
	spinlock_t	    lock;
};

struct picogpio_pin {
	struct list_head    list;
	unsigned	    gpio;
	int		    is_input;
};

static int picogpio_new_pin(struct file *filp, unsigned gpio)
{
	int ret;
	struct picogpio_pin *pin = kmalloc(sizeof(*pin), GFP_KERNEL);
	struct picogpio_session *session = filp->private_data;

	if (!pin)
		return -ENOMEM;

	INIT_LIST_HEAD(&pin->list);
	pin->gpio	= gpio;
	pin->is_input	= 0;

	ret = gpio_request(gpio, "picogpio/ioctl");
	if (ret)
		kfree(pin);
	else
		list_add(&pin->list, &session->list);

	return ret;
}

static struct picogpio_pin *picogpio_find_pin(struct file *filp,
					      unsigned gpio)
{
	struct picogpio_session *session = filp->private_data;
	struct picogpio_pin *i, *ret = NULL;

	list_for_each_entry(i, &session->list, list) {
		if (gpio == i->gpio) {
			ret = i;
			break;
		}
	}

	return ret;
}

static int picogpio_free_pin(struct file *filp, unsigned gpio)
{
	struct picogpio_pin *pin = picogpio_find_pin(filp, gpio);
	if (!pin)
		return -EINVAL;

	list_del(&pin->list);
	gpio_free(pin->gpio);
	kfree(pin);

	return 0;
}

static int picogpio_set_direction(struct file *filp, unsigned gpio,
				  int direction, int value)
{
	struct picogpio_pin *pin = picogpio_find_pin(filp, gpio);
	int ret;

	if (!pin)
		return -EINVAL;

	if (PICOGPIO_INPUT == direction)
		ret = gpio_direction_input(gpio);
	else if (PICOGPIO_OUTPUT == direction)
		ret = gpio_direction_output(gpio, value);
	else
		ret = -EINVAL;

	if (!ret)
		pin->is_input = (PICOGPIO_INPUT == direction);

	return ret;
}

static long picogpio_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	int ret;
	picogpio_op_t op;
	picogpio_analogue_config_t dac_cfg;
	struct picogpio_session *session = filp->private_data;
	struct picogpio_pin *pin;

	if (_IOC_TYPE(cmd) != PICOGPIO_IOCTL_BASE) {
		pr_debug("invalid command type (%u)\n", cmd);
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) >
	    (PICOGPIO_IOCTL_START + PICOGPIO_IOCTL_NUM_IOCTL) ||
	    _IOC_NR(cmd) < (PICOGPIO_IOCTL_START)) {
		pr_debug("invalid command (%u)\n", cmd);
		return -ENOTTY;
	}

	if (cmd != PICOGPIO_ANALOGUE_CONFIG)
		ret = copy_from_user(&op, (void __user *)arg, sizeof(op));
	else
		ret = copy_from_user(&dac_cfg, (void __user *)arg,
				     sizeof(dac_cfg));

	if (ret) {
		pr_debug("failed to copy structure\n");
		return -EFAULT;
	}

	spin_lock(&session->lock);

	switch (cmd) {
	case PICOGPIO_ACQUIRE:
		ret = picogpio_new_pin(filp, op.pin);
		break;

	case PICOGPIO_RELEASE:
		ret = picogpio_free_pin(filp, op.pin);
		break;

	case PICOGPIO_SET_DIRECTION:
		ret = picogpio_set_direction(filp, op.pin, op.value,
					     op.def_value);
		break;

	case PICOGPIO_GET_DIRECTION:
		pin = picogpio_find_pin(filp, op.pin);
		if (pin) {
			op.value =
			    pin->is_input ? PICOGPIO_INPUT : PICOGPIO_OUTPUT;
			ret = copy_to_user((void __user *)arg, &op, sizeof(op));
		}
		break;

	case PICOGPIO_SET_VALUE:
		gpio_set_value(op.pin, op.value);
		ret = 0;
		break;

	case PICOGPIO_GET_VALUE:
		ret = gpio_get_value(op.pin);
		if (ret >= 0) {
			op.value = ret;
			ret = copy_to_user((void __user *)arg, &op, sizeof(op));
		}
		break;

	case PICOGPIO_ANALOGUE_CONFIG:
		ret = picoxcell_gpio_configure_dac(dac_cfg.pin,
						   dac_cfg.converter_size,
						   dac_cfg.analogue_rate);
		break;

	default:
		pr_debug("invalid ioctl(), cmd=%d\n", cmd);
		ret = -ENOTTY;
		break;
	}

	spin_unlock(&session->lock);

	return ret;
}

static int picogpio_open(struct inode *inode, struct file *filp)
{
	struct picogpio_session *session = kmalloc(sizeof(*session),
						   GFP_KERNEL);
	if (!session)
		return -ENOMEM;
	spin_lock_init(&session->lock);
	INIT_LIST_HEAD(&session->list);
	filp->private_data = session;

	return 0;
}

static int picogpio_release(struct inode *inode, struct file *filp)
{
	struct picogpio_pin *pos, *tmp;
	struct picogpio_session *session = filp->private_data;

	list_for_each_entry_safe(pos, tmp, &session->list, list) {
		gpio_free(pos->gpio);
		kfree(pos);
	}
	kfree(session);

	return 0;
}

static int picogpio_init(void)
{
	return misc_register(&picogpio_dev);
}
module_init(picogpio_init);

static void picogpio_exit(void)
{
	misc_deregister(&picogpio_dev);
}
module_exit(picogpio_exit);

MODULE_AUTHOR("Jamie Iles");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("picogpio userspace GPIO interface");

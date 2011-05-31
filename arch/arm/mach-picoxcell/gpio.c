/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "picoxcellgpio: " fmt

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <mach/hardware.h>

#include "mux.h"

static struct {
	struct picoxcell_gpio_bank	*banks;
	int				nr_banks;
	int				nr_sdgpio;
} sdgpio;

/* The base address of SD-GPIO config registers in the AXI2Pico. */
#define SD_PIN_CONFIG_BASE		0x9800
/* The base address of SD-GPIO analogue value registers in the AXI2Pico. */
#define SD_PIN_ANALOGUE_VALUE_BASE	0x9801
/* The base address of SD-GPIO analogue rate registers in the AXI2Pico. */
#define SD_PIN_ANALOGUE_RATE_BASE	0x9802
/* The address of the control value register in the AXI2Pico. */
#define SD_CONTROL_VAL_REG		0x9882
/* The address of the control value high register in the AXI2Pico (pc3x3). */
#define SD_CONTROL_VAL_HI_REG		0x9883
/* The address of the output value register in the AXI2Pico. */
#define SD_OUTPUT_VAL_REG		0x9884
/* The address of the output value high register in the AXI2Pico (pc3x3). */
#define SD_OUTPUT_HI_VAL_REG		0x9885
/* The address of the input value register in the AXI2Pico. */
#define SD_INPUT_VAL_REG		0x9880
/* The address of the input value high register in the AXI2Pico (pc3x3). */
#define SD_INPUT_VAL_HI_REG		0x9880
/* The address of the sleep register in the AXI2Pico. */
#define PICOXCELL_AXI2PICO_SLEEP_REG	0xA060
/* The spacing between SD-GPIO config registers. */
#define SD_PIN_CONFIG_SPACING		4
/* Control source bit. */
#define SD_CONFIG_CS_MASK		(~(1 << 15))
/* Analogue not digital bit. */
#define SD_CONFIG_AND			(1 << 14)
/* The mask for analogue converter size in the config register. */
#define SD_CONV_SZ_MASK			0xF
/* Soft reset lock bit. */
#define SD_CONFIG_SR_LOCK		(1 << 13)
/* PICOXCELL AXI2Pico CAEID. */
#define PICOXCELL_AXI2PICO_CAEID	0x9000

/*
 * Get the address of a config register for a SD-GPIO pin.
 *
 * @_n The SD-GPIO pin number.
 *
 * Returns the base address of the register.
 */
#define SD_PIN_CONFIG(_n) \
	(SD_PIN_CONFIG_BASE + ((_n) * SD_PIN_CONFIG_SPACING))

/*
 * Get the address of a analogue rate register for a SD-GPIO pin.
 *
 * @_n The SD-GPIO pin number.
 *
 * Returns the base address of the register.
 */
#define SD_PIN_ANALOGUE_RATE(_n) \
	(SD_PIN_ANALOGUE_RATE_BASE + ((_n) * SD_PIN_CONFIG_SPACING))

/*
 * Get the address of a analogue value register for a SD-GPIO pin.
 *
 * @_n The SD-GPIO pin number.
 *
 * Returns the base address of the register.
 */
#define SD_PIN_ANALOGUE_VAL(_n) \
	(SD_PIN_ANALOGUE_VALUE_BASE + ((_n) * SD_PIN_CONFIG_SPACING))

static int sdgpio_reset_config(unsigned block_pin, int value)
{
	int ret;
	u16 data;

	ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
				  SD_PIN_CONFIG(block_pin), &data, 1);
	if (1 != ret) {
		pr_err("failed to read config register for SDGPIO pin %u\n",
		       block_pin);
		return -EIO;
	}

	if (value)
		data |= SD_CONFIG_SR_LOCK;
	else
		data &= ~SD_CONFIG_SR_LOCK;

	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
			     SD_PIN_CONFIG(block_pin), &data, 1);

	return 0;
}

static inline int sdgpio_block_nr(int gpio)
{
	int i;

	for (i = 0; i < sdgpio.nr_banks; ++i) {
		struct picoxcell_gpio_bank *bank = &sdgpio.banks[i];

		if (gpio >= bank->gpio_start &&
		    gpio < bank->gpio_start + bank->nr_pins)
			return (gpio - bank->gpio_start) + bank->block_base;
	}

	return -EINVAL;
}

static int sdgpio_request(struct gpio_chip *chip, unsigned offset)
{
	unsigned block_pin = sdgpio_block_nr(chip->base + offset);

	if (sdgpio_reset_config(block_pin, 1))
		return -EIO;

	return 0;
}

static void sdgpio_free(struct gpio_chip *chip, unsigned offset)
{
	picoxcell_gpio_configure_dac(chip->base + offset, 0, 0);
}

/*
 * Create a map of which pins are analogue and not digital. We have a separate
 * function for configuring pins as analogue. When we set analogue pins, we
 * don't treat the int parameter as a boolean anymore.
 */
static DECLARE_BITMAP(a_not_d_map, ARCH_NR_GPIOS);

static int sdgpio_get_digital_out_status(u32 *v)
{
	u16 data[2] = { 0, 0 };

	if (1 != axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
				     SD_OUTPUT_VAL_REG, &data[0], 1))
		return -EIO;

	if (sdgpio.nr_sdgpio > 16) {
		if (1 != axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
					SD_OUTPUT_HI_VAL_REG, &data[1], 1))
			return -EIO;
	}

	*v = data[0] | (data[1] << 16);

	return 0;
}

static void sdgpio_set_digital_out_status(u32 v)
{
	u16 data[2] = { (u16)(v & 0xFFFF), (u16)((v >> 16) & 0xFFFF) };

	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID, SD_OUTPUT_VAL_REG,
			     &data[0], 1);

	if (sdgpio.nr_sdgpio > 16) {
		axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
				     SD_OUTPUT_HI_VAL_REG, &data[1], 1);
	}
}

static void sdgpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	u16 data;
	unsigned block_pin = sdgpio_block_nr(chip->base + offset);

	if (!test_bit(chip->base + offset, a_not_d_map)) {
		u32 status;

		if (sdgpio_get_digital_out_status(&status)) {
			pr_err("failed to read SDGPIO output value reg\n");
			return;
		}

		status &= ~(1 << block_pin);
		status |= (!!value) << block_pin;

		sdgpio_set_digital_out_status(status);
	} else {
		/* Analogue mode */
		data = (u16)value;
		axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
				     SD_PIN_ANALOGUE_VAL(block_pin), &data, 1);
	}
}

static int sdgpio_get_digital_in_status(u32 *v)
{
	u16 data[2] = { 0, 0 };

	if (1 != axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID, SD_INPUT_VAL_REG,
				     &data[0], 1))
		return -EIO;

	if (sdgpio.nr_sdgpio > 16) {
		if (1 != axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
					     SD_INPUT_VAL_HI_REG, &data[1], 1))
			return -EIO;
	}

	*v = data[0] | (data[1] << 16);

	return 0;
}

static int sdgpio_get(struct gpio_chip *chip, unsigned offset)
{
	int ret;
	u16 data;
	unsigned block_pin = sdgpio_block_nr(chip->base + offset);

	if (!test_bit(chip->base + offset, a_not_d_map)) {
		u32 status;

		if (sdgpio_get_digital_in_status(&status))
			return -EIO;

		return !!(status & (1 << block_pin));
	} else {
		/* Analogue mode */
		ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
					  SD_PIN_ANALOGUE_VAL(block_pin),
					  &data, 1);
		if (1 != ret) {
			pr_err("failed to read the analogue value register for SDGPIO pin %u\n",
			       block_pin);
			return -EIO;
		}

		return (int)data;
	}
}

static int sdgpio_set_direction(unsigned block_pin, int input)
{
	int ret;
	u16 data;

	ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
				  SD_PIN_CONFIG(block_pin), &data, 1);
	if (1 != ret) {
		pr_err("failed to read config register for SDGPIO pin %u\n",
		       block_pin);
		return -EIO;
	}

	data &= SD_CONFIG_CS_MASK;
	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
			     SD_PIN_CONFIG(block_pin), &data, 1);

	/* Configure the pin to drive or not drive the output as appropriate. */
	ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
				  SD_CONTROL_VAL_REG, &data, 1);
	if (1 != ret) {
		pr_err("failed to read SDGPIO control value register\n");
		return -EIO;
	}

	if (input)
		data &= ~(1 << block_pin);
	else
		data |= (1 << block_pin);

	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID, SD_CONTROL_VAL_REG,
			     &data, 1);

	return 0;
}

static int sdgpio_direction_output(struct gpio_chip *chip, unsigned offset,
				   int value)
{
	unsigned block_pin = sdgpio_block_nr(chip->base + offset);
	int ret = sdgpio_set_direction(block_pin, 0);

	if (ret)
		return ret;

	sdgpio_set(chip, offset, value);

	return 0;
}

static int sdgpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned block_pin = sdgpio_block_nr(chip->base + offset);

	return sdgpio_set_direction(block_pin, 1);
}

int picoxcell_gpio_configure_dac(unsigned gpio, u8 converter_size,
				 u16 analogue_rate)
{
	int ret;
	u16 data;
	unsigned block_pin = sdgpio_block_nr(gpio);

	ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID,
				  SD_PIN_CONFIG(block_pin), &data, 1);
	if (1 != ret) {
		pr_err("failed to read config register for SDGPIO pin %u\n",
		       block_pin);
		return -EIO;
	}

	data &= SD_CONFIG_CS_MASK | ~SD_CONV_SZ_MASK;
	if (!analogue_rate && !converter_size)
		data &= ~SD_CONFIG_AND;
	else
		data |= SD_CONFIG_AND;
	data |= (converter_size & SD_CONV_SZ_MASK);

	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
			     SD_PIN_CONFIG(block_pin), &data, 1);

	/* Configure the pin to drive the output. */
	ret = axi2cfg_config_read(PICOXCELL_AXI2PICO_CAEID, SD_CONTROL_VAL_REG,
				  &data, 1);
	if (1 != ret) {
		pr_err("failed to read SDGPIO control value register\n");
		return -EIO;
	}

	data |= (1 << block_pin);

	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID, SD_CONTROL_VAL_REG,
			     &data, 1);

	/* Write the analogue rate register */
	data = analogue_rate;
	axi2cfg_config_write(PICOXCELL_AXI2PICO_CAEID,
			     SD_PIN_ANALOGUE_RATE(block_pin), &data, 1);

	if (analogue_rate || converter_size)
		set_bit(gpio, a_not_d_map);
	else
		clear_bit(gpio, a_not_d_map);

	return 0;
}
EXPORT_SYMBOL_GPL(picoxcell_gpio_configure_dac);

static int sdgpio_add_bank(struct platform_device *pdev,
			   struct picoxcell_gpio_bank *bank)
{
	struct gpio_chip *chip;
	int err;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->owner		= THIS_MODULE;
	chip->label		= bank->label;
	chip->request		= sdgpio_request;
	chip->free		= sdgpio_free;
	chip->direction_output	= sdgpio_direction_output;
	chip->direction_input	= sdgpio_direction_input;
	chip->get		= sdgpio_get;
	chip->set		= sdgpio_set;
	chip->base		= bank->gpio_start;
	chip->names		= bank->names;
	chip->ngpio		= bank->nr_pins;
	sdgpio.nr_sdgpio += chip->ngpio;

	err = gpiochip_add(chip);
	if (err) {
		pr_err("failed to add sdgpio chip %s..%s\n", bank->names[0],
		       bank->names[bank->nr_pins - 1]);
		kfree(chip);
	} else
		pr_info("registered SD gpio bank %s..%s (%d..%d)\n",
			bank->names[0], bank->names[bank->nr_pins - 1],
			chip->base, chip->base + chip->ngpio - 1);

	return err;
}

static int __init sdgpio_probe(struct platform_device *pdev)
{
	struct sdgpio_platform_data *pdata = pdev->dev.platform_data;
	int i;

	if (!pdata)
		return -ENODEV;

	sdgpio.banks		= pdata->banks;
	sdgpio.nr_banks		= pdata->nr_banks;

	for (i = 0; i < pdata->nr_banks; ++i) {
		if (sdgpio_add_bank(pdev, &pdata->banks[i]))
			dev_warn(&pdev->dev, "unable to register bank %d\n", i);
	}

	return 0;
}

static struct platform_driver sdgpio_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "sdgpio",
	},
};

static int __init sdgpio_init(void)
{
	return platform_driver_probe(&sdgpio_driver, sdgpio_probe);
}
device_initcall(sdgpio_init);

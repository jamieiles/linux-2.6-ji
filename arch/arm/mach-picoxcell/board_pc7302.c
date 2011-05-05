/*
 * linux/arch/arm/mach-picoxcell/board_pc7302.c
 *
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand-gpio.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>

#include <mach/hardware.h>
#include <mach/picoxcell/axi2cfg.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "mux.h"
#include "picoxcell_core.h"

static long pc7302_panic_blink(int state)
{
	__raw_writel(state ? 0xFF : 0, IO_ADDRESS(PICOXCELL_GPIO_BASE +
					    GPIO_SW_PORT_C_DR_REG_OFFSET));
	return 0;
}

static void pc7302_panic_init(void)
{
	/*
	 * We have a BOOT_ERROR pin on PC7302. Reuse that for signalling when
	 * the kernel panics. There is only 1 bit wired up to port C but it
	 * won't hurt to configure all of them.
	 */
	__raw_writel(0xF, IO_ADDRESS(PICOXCELL_GPIO_BASE +
			       GPIO_SW_PORT_C_DDR_REG_OFFSET));
	__raw_writel(0x0, IO_ADDRESS(PICOXCELL_GPIO_BASE +
			       GPIO_SW_PORT_C_CTL_REG_OFFSET));

	panic_blink = pc7302_panic_blink;
}

static struct mtd_partition pc7302_nor_partitions[] = {
	{
		.name		= "Boot",
		.size		= SZ_256K,
		.offset		= 0,
	},
	{
		.name		= "Boot Environment",
		.size		= SZ_128K,
		.offset		= MTDPART_OFS_APPEND,
	},
	{
		.name		= "Kernel",
		.size		= SZ_4M,
		.offset		= MTDPART_OFS_APPEND,
	},
	{
		.name		= "Application",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	},
};

static struct physmap_flash_data pc7302_nor_flash_data = {
	.width		= 1,
	.parts		= pc7302_nor_partitions,
	.nr_parts	= ARRAY_SIZE(pc7302_nor_partitions)
};

static struct resource pc7302_nor_resource = {
	.start	= PICOXCELL_FLASH_BASE,
	.end	= PICOXCELL_FLASH_BASE + SZ_128M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device pc7302_nor = {
	.name		    = "physmap-flash",
	.id		    = -1,
	.dev.platform_data  = &pc7302_nor_flash_data,
	.resource	    = &pc7302_nor_resource,
	.num_resources	    = 1,
};

static void pc7302_init_nor(void)
{
	struct clk *ebi_clk = clk_get(NULL, "ebi");

	if (IS_ERR(ebi_clk)) {
		pr_err("failed to get EBI clk, unable to register NOR flash\n");
		return;
	}

	if (clk_enable(ebi_clk)) {
		pr_err("failed to enable EBI clk, unable to register NOR flash\n");
		clk_put(ebi_clk);
		return;
	}

	platform_device_register(&pc7302_nor);
}

static struct resource pc7302_nand_resource = {
	.start = EBI_CS2_BASE,
	.end   = EBI_CS2_BASE + 2 * SZ_1K,
	.flags = IORESOURCE_MEM,
};

static struct mtd_partition pc7302_nand_parts[] = {
	{
		.name	= "Boot",
		.size	= 4 * SZ_128K,
		.offset	= 0,
	},
	{
		.name	= "Redundant Boot",
		.size	= 4 * SZ_128K,
		.offset	= MTDPART_OFS_APPEND,
	},
	{
		.name	= "Boot Environment",
		.size	= SZ_128K,
		.offset	= MTDPART_OFS_APPEND,
	},
	{
		.name	= "Redundant Boot Environment",
		.size	= SZ_128K,
		.offset	= MTDPART_OFS_APPEND,
	},
	{
		.name	= "Kernel",
		.size	= 8 * SZ_1M,
		.offset	= (12 * SZ_128K),
	},
	{
		.name	= "File System",
		.size	= MTDPART_SIZ_FULL,
		.offset	= MTDPART_OFS_APPEND,
	},
};

static struct gpio_nand_platdata pc7302_nand_platdata = {
	.gpio_rdy   = PC3X2_GPIO_PIN_ARM_1,
	.gpio_nce   = PC3X2_GPIO_PIN_ARM_2,
	.gpio_ale   = PC3X2_GPIO_PIN_ARM_3,
	.gpio_cle   = PC3X2_GPIO_PIN_ARM_4,
	.gpio_nwp   = -1,
	.parts	    = pc7302_nand_parts,
	.num_parts  = ARRAY_SIZE(pc7302_nand_parts),
};

static struct platform_device pc7302_nand = {
	.name		    = "gpio-nand",
	.num_resources	    = 1,
	.resource	    = &pc7302_nand_resource,
	.id		    = -1,
	.dev.platform_data  = &pc7302_nand_platdata,
};

static void pc7302_init_nand(void)
{
	struct clk *ebi_clk = clk_get(NULL, "ebi");
	int err;
	const struct mux_cfg pc3x2_cfg[] = {
		MUXCFG("arm4", MUX_ARM),
	};
	const struct mux_cfg pc3x3_cfg[] = {
		MUXCFG("pai_tx_data0", MUX_PERIPHERAL_PAI),
		MUXCFG("ebi_addr22", MUX_ARM),
	};

	if (picoxcell_is_pc3x3())
		err = mux_configure_table(pc3x3_cfg, ARRAY_SIZE(pc3x3_cfg));
	else
		err = mux_configure_table(pc3x2_cfg, ARRAY_SIZE(pc3x2_cfg));
	if (err) {
		pr_err("unable to set ebi_addr22 for use as gpio-nand cle\n");
		return;
	}

	if (IS_ERR(ebi_clk)) {
		pr_err("failed to get EBI clk, unable to register NAND flash\n");
		return;
	}

	if (clk_enable(ebi_clk)) {
		pr_err("failed to enable EBI clk, unable to register NAND flash\n");
		clk_put(ebi_clk);
		return;
	}

	platform_device_register(&pc7302_nand);
}

FIXED_CLK(pc7302_uart,	3686400, -1, NULL);
static struct clk_lookup pc7302_uart_lookup = CLK_LOOKUP(NULL, "uart",
							 &pc7302_uart_clk);

static void pc7302_register_uarts(void)
{
	picoxcell_clk_add(&pc7302_uart_clk);
	clkdev_add(&pc7302_uart_lookup);
	picoxcell_add_uart(PICOXCELL_UART1_BASE, IRQ_UART1, 0);
	picoxcell_add_uart(PICOXCELL_UART2_BASE, IRQ_UART2, 1);
}

static void __init pc7302_init(void)
{
	picoxcell_tsu_init(20000000);
	picoxcell_core_init();

	pc7302_register_uarts();

	if ((axi2cfg_readl(AXI2CFG_SYSCFG_REG_OFFSET) & 0x3) == 0)
		pc7302_init_nor();
	else
		pc7302_init_nand();

	pc7302_panic_init();
}

MACHINE_START(PC7302, "PC7302")
	.map_io		= picoxcell_map_io,
	.init_irq	= picoxcell_init_irq,
	.init_early	= picoxcell_init_early,
	.timer		= &picoxcell_sys_timer,
	.init_machine	= pc7302_init,
MACHINE_END

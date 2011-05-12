/*
 * linux/arch/arm/mach-picoxcell/board_pc7308.c
 *
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/picoxcell/axi2cfg.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "mux.h"
#include "picoxcell_core.h"

static struct mtd_partition pc7308_nand_parts[] = {
	{
		.name	= "Boot",
		.size	= 4 * SZ_128K,
		.offset	= 8 * SZ_128K,
	},
	{
		.name	= "Redundant Boot",
		.size	= 4 * SZ_128K,
		.offset	= 16 * SZ_128K,
	},
	{
		.name	= "Boot Environment",
		.size	= SZ_128K,
		.offset	= 24 * SZ_128K,
	},
	{
		.name	= "Redundant Boot Environment",
		.size	= SZ_128K,
		.offset	= MTDPART_OFS_APPEND,
	},
	{
		.name	= "Kernel",
		.size	= 8 * SZ_1M,
		.offset	= (28 * SZ_128K),
	},
	{
		.name	= "File System",
		.size	= MTDPART_SIZ_FULL,
		.offset	= MTDPART_OFS_APPEND,
	},
};

static void pc7308_init_nand(void)
{
	int err = picoxcell_add_hw_nand(pc7308_nand_parts,
					ARRAY_SIZE(pc7308_nand_parts));
	if (err)
		pr_err("failed to register nand partitions\n");
}

static void pc7308_register_uarts(void)
{
	int err;
	struct platform_device *pdev;

	pdev = picoxcell_add_uart(PICOXCELL_UART1_BASE, IRQ_UART1, 0);
	if (IS_ERR(pdev))
		pr_err("failed to add uart0\n");

	pdev = picoxcell_add_uart(PICOXCELL_UART2_BASE, IRQ_UART2, 1);
	if (IS_ERR(pdev))
		pr_err("failed to add uart1\n");

	err = picoxcell_add_uicc(PC30XX_UART3_BASE, IRQ_PC30XX_UART3, 2,
				 false);
	if (err)
		pr_err("failed to add uart based uicc controller\n");
}

static void __init pc7308_init(void)
{
	picoxcell_tsu_init(20000000);
	picoxcell_core_init();

	pc7308_register_uarts();
	pc7308_init_nand();
}

MACHINE_START(PC7308, "PC7308")
	.map_io		= picoxcell_map_io,
	.init_irq	= picoxcell_init_irq,
	.init_early	= picoxcell_init_early,
	.timer		= &picoxcell_sys_timer,
	.init_machine	= pc7308_init,
MACHINE_END

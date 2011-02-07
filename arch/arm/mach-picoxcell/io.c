/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"

static struct map_desc __initdata picoxcell_io_desc[] = {
	{
		.virtual	= PHYS_TO_IO(PICOXCELL_PERIPH_BASE),
		.pfn		= __phys_to_pfn(PICOXCELL_PERIPH_BASE),
		.length		= PICOXCELL_PERIPH_LENGTH,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= SRAM_VIRT,
		.pfn		= __phys_to_pfn(SRAM_BASE),
		.length		= SRAM_SIZE,
		.type		= MT_MEMORY,
	},
};

void __init picoxcell_map_io(void)
{
	iotable_init(picoxcell_io_desc, ARRAY_SIZE(picoxcell_io_desc));
}

/*
 * Intercept ioremap() requests for addresses in our fixed mapping regions.
 */
void __iomem *picoxcell_ioremap(unsigned long p, size_t size, unsigned int type)
{
	if (p >= PICOXCELL_PERIPH_BASE &&
	    p < PICOXCELL_PERIPH_BASE + PICOXCELL_PERIPH_LENGTH)
		return IO_ADDRESS(p);

	return __arm_ioremap_caller(p, size, type, __builtin_return_address(0));
}
EXPORT_SYMBOL(picoxcell_ioremap);

void picoxcell_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL(picoxcell_iounmap);

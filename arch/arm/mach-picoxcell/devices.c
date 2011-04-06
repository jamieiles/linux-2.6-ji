/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/dma-mapping.h>
#include <linux/dw_dmac.h>
#include <linux/platform_device.h>
#include <linux/platform_data/macb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#include <asm/pmu.h>

#include "picoxcell_core.h"
#include "soc.h"

static struct resource pmu_resource = {
	.start			= IRQ_NPMUIRQ,
	.end			= IRQ_NPMUIRQ,
	.flags			= IORESOURCE_IRQ,
};

static struct platform_device pmu_device = {
	.name			= "arm-pmu",
	.id			= ARM_PMU_DEVICE_CPU,
	.num_resources		= 1,
	.resource		= &pmu_resource,
};

static struct resource eth_resources[] = {
	[0] = {
		.start		= PICOXCELL_EMAC_BASE,
		.end		= PICOXCELL_EMAC_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_EMAC,
		.end		= IRQ_EMAC,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 eth_dmamask = DMA_BIT_MASK(32);
static struct macb_platform_data eth_data;

static struct platform_device eth_device = {
	.name			= "macb",
	.id			= -1,
	.dev			= {
		.dma_mask	= &eth_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data	= &eth_data,
	},
	.resource		= eth_resources,
	.num_resources		= ARRAY_SIZE(eth_resources),
};

static struct resource dmac0_resources[] = {
	{
		.start		= PICOXCELL_DMAC1_BASE,
		.end		= PICOXCELL_DMAC1_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_DMAC1,
		.end		= IRQ_DMAC1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct dw_dma_platform_data dmac0_pdata = {
	.nr_channels		= 8,
};

static u64 dmac0_dmamask = DMA_BIT_MASK(32);

static struct platform_device dmac0_device = {
	.name			= "dw_dmac",
	.id			= 0,
	.resource		= dmac0_resources,
	.num_resources		= ARRAY_SIZE(dmac0_resources),
	.dev			= {
		.dma_mask		= &dmac0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &dmac0_pdata,
	},
};

static struct resource dmac1_resources[] = {
	{
		.start		= PICOXCELL_DMAC2_BASE,
		.end		= PICOXCELL_DMAC2_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_DMAC2,
		.end		= IRQ_DMAC2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct dw_dma_platform_data dmac1_pdata = {
	.nr_channels		= 8,
	.is_private		= 1,
};

static u64 dmac1_dmamask = DMA_BIT_MASK(32);

static struct platform_device dmac1_device = {
	.name			= "dw_dmac",
	.id			= 1,
	.resource		= dmac1_resources,
	.num_resources		= ARRAY_SIZE(dmac1_resources),
	.dev			= {
		.dma_mask		= &dmac1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &dmac1_pdata,
	},
};

static struct platform_device *common_devices[] __initdata = {
	&pmu_device,
	&eth_device,
	&dmac0_device,
	&dmac1_device,
};

static int __init picoxcell_add_devices(void)
{
	platform_add_devices(common_devices, ARRAY_SIZE(common_devices));

	return 0;
}
arch_initcall(picoxcell_add_devices);

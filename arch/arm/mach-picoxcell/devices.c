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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/picochip/picoif.h>
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
	.is_private		= true,
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
	.is_private		= true,
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

static struct resource pa0_resources[] = {
	{
		.start		= AXI2PICO_BUFFERS_BASE,
		.end		= AXI2PICO_BUFFERS_BASE +
				  AXI2PICO_BUFFERS_SIZE - 1,
		.flags		= IORESOURCE_MEM,
		.name		= "ahb2pico_axi2pico",
	},
	{
		.start		= PICOXCELL_AXI2CFG_BASE +
				  AXI2CFG_PURGE_CFG_PORT_REG_OFFSET,
		.end		= PICOXCELL_AXI2CFG_BASE +
				  AXI2CFG_DEVICE_ID_REG_OFFSET - 1,
		.flags		= IORESOURCE_MEM,
		.name		= "procif",
	},
	{
		.start		= PICOXCELL_AXI2CFG_BASE +
				  AXI2CFG_CONFIG_WRITE_REG_OFFSET,
		.end		= PICOXCELL_AXI2CFG_BASE +
				  AXI2CFG_DMAC1_CONFIG_REG_OFFSET - 1,
		.flags		= IORESOURCE_MEM,
		.name		= "procif2",
	},
	{
		.start		= IRQ_AXI2PICO8,
		.end		= IRQ_AXI2PICO8,
		.flags		= IORESOURCE_IRQ,
		.name		= "gpr_irq",
	},
};

static struct pc3xx_pdata pa0_pdata = {
	.axi2pico_dmac		= &dmac0_device.dev,
	.axi2cfg_dmac		= &dmac1_device.dev,
};

static struct platform_device pa0 = {
	.name			= "picoArray",
	.id			= 0,
	.dev.coherent_dma_mask	= 0xffffffff,
	.resource		= pa0_resources,
	.num_resources		= ARRAY_SIZE(pa0_resources),
	.dev.platform_data	= &pa0_pdata,
};

static struct resource spi_resources[] = {
	{
		.start		= PICOXCELL_SSI_BASE,
		.end		= PICOXCELL_SSI_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_SSI,
		.end		= IRQ_SSI,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device spi_device = {
	.name			= "picoxcell-spi",
	.id			= 0,
	.resource		= spi_resources,
	.num_resources		= ARRAY_SIZE(spi_resources),
};

static struct platform_device *common_devices[] __initdata = {
	&pmu_device,
	&dmac0_device,
	&dmac1_device,
	&pa0,
	&spi_device,
};

static int __init picoxcell_add_devices(void)
{
	platform_add_devices(common_devices, ARRAY_SIZE(common_devices));

	return 0;
}
arch_initcall(picoxcell_add_devices);

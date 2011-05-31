/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#include <asm/pmu.h>

#include "picoxcell_core.h"
#include "soc.h"

#define UART_USR_REG_OFFSET			0x7C
static struct plat_serial8250_port serial1_platform_data[] = {
	{
		.membase	= IO_ADDRESS(PICOXCELL_UART1_BASE),
		.mapbase	= PICOXCELL_UART1_BASE,
		.irq		= IRQ_UART1,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_DWAPB32,
		.regshift	= 2,
		.uartclk	= PICOXCELL_BASE_BAUD,
		.private_data	= (void *)(PHYS_TO_IO(PICOXCELL_UART1_BASE +
						      UART_USR_REG_OFFSET)),
	},
	{},
};

static struct resource serial1_resources[] = {
	{
		.start		= PICOXCELL_UART1_BASE,
		.end		= PICOXCELL_UART1_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_UART1,
		.end		= IRQ_UART2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device serial1_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM1,
	.dev.platform_data	= serial1_platform_data,
	.resource		= serial1_resources,
	.num_resources		= ARRAY_SIZE(serial1_resources),
};

static struct plat_serial8250_port serial2_platform_data[] = {
	{
		.membase	= IO_ADDRESS(PICOXCELL_UART2_BASE),
		.mapbase	= PICOXCELL_UART2_BASE,
		.irq		= IRQ_UART2,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_DWAPB32,
		.regshift	= 2,
		.uartclk	= PICOXCELL_BASE_BAUD,
		.private_data	= (void *)(PHYS_TO_IO(PICOXCELL_UART2_BASE +
						      UART_USR_REG_OFFSET)),
	},
	{},
};

static struct resource serial2_resources[] = {
	{
		.start		= PICOXCELL_UART2_BASE,
		.end		= PICOXCELL_UART2_BASE + 0xFFFF,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_UART2,
		.end		= IRQ_UART2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device serial2_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM2,
	.dev.platform_data	= serial2_platform_data,
	.resource		= serial2_resources,
	.num_resources		= ARRAY_SIZE(serial2_resources),
};

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

static struct platform_device *common_devices[] __initdata = {
	&serial1_device,
	&serial2_device,
	&pmu_device,
};

static int __init picoxcell_add_devices(void)
{
	platform_add_devices(common_devices, ARRAY_SIZE(common_devices));

	return 0;
}
arch_initcall(picoxcell_add_devices);

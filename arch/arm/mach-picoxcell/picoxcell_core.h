/*
 * linux/arch/arm/mach-picoxcell/picoxcell_core.h
 *
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#ifndef __ASM_ARCH_PICOXCELL_CORE_H__
#define __ASM_ARCH_PICOXCELL_CORE_H__

struct picoxcell_soc;
struct sys_timer;

extern void picoxcell_init_early(void);
extern void picoxcell_core_init(void);
extern void picoxcell_init_irq(void);
extern void picoxcell_map_io(void);
extern struct sys_timer picoxcell_sys_timer;
extern void picoxcell_sched_clock_init(void);

extern int picoxcell_add_gpio_port(int port, int ngpio, int base);

#endif /* __ASM_ARCH_PICOXCELL_CORE_H__ */

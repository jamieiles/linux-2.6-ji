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

#ifdef CONFIG_PICOXCELL_HAVE_TSU
void picoxcell_tsu_init(unsigned long rate);
#else /* CONFIG_PICOXCELL_HAVE_TSU */
static inline void picoxcell_tsu_init(unsigned long rate) {}
#endif /* CONFIG_PICOXCELL_HAVE_TSU */

extern void __init armgpio_irq_init(void);
extern int picoxcell_add_uart(unsigned long addr, int irq, int id);
int __init picoxcell_add_spacc(const char *name, unsigned long addr, int irq,
			       int id);
int __init picoxcell_add_trng(unsigned long addr);

struct picoxcell_fuse_map;
extern int __init picoxcell_add_fuse(struct picoxcell_fuse_map *map);

#endif /* __ASM_ARCH_PICOXCELL_CORE_H__ */

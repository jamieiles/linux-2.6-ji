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

extern int picoxcell_add_gpio_port(int port, int ngpio, int base,
				   const char *const *names);

#ifdef CONFIG_PICOXCELL_HAVE_TSU
void picoxcell_tsu_init(unsigned long rate);
#else /* CONFIG_PICOXCELL_HAVE_TSU */
static inline void picoxcell_tsu_init(unsigned long rate) {}
#endif /* CONFIG_PICOXCELL_HAVE_TSU */

extern void __init armgpio_irq_init(void);
extern struct platform_device *picoxcell_add_uart(unsigned long addr, int irq,
						  int id);
int __init picoxcell_add_spacc(const char *name, unsigned long addr, int irq,
			       int id);
int __init picoxcell_add_trng(unsigned long addr);
extern int __init picoxcell_add_emac(unsigned long addr, int irq,
				     unsigned long quirks);

struct picoxcell_fuse_map;
extern int __init picoxcell_add_fuse(struct picoxcell_fuse_map *map);
extern int picoxcell_fuse_read(unsigned long addr, char *buf,
			       size_t nr_bytes);
extern int __init picoxcell_add_uicc(unsigned long addr, int irq, int id,
				     bool data_invert);

struct mtd_partition;
#ifdef CONFIG_PC30XX_HW_NAND
extern int __init picoxcell_add_hw_nand(const struct mtd_partition *parts,
					unsigned int nr_parts);
#else /* CONFIG_PC30XX_HW_NAND */
static inline int picoxcell_add_hw_nand(const struct mtd_partition *parts,
					unsigned int nr_parts)
{
	return -ENODEV;
}
#endif /* CONFIG_PC30XX_HW_NAND */

extern int picoxcell_is_pc3x2(void);
extern int picoxcell_is_pc3x3(void);
extern int picoxcell_is_pc30xx(void);

#endif /* __ASM_ARCH_PICOXCELL_CORE_H__ */

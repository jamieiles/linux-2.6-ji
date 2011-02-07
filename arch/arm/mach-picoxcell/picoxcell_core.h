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

extern void picoxcell_init_early(void);
extern void picoxcell_core_init(void);
extern void picoxcell_init_irq(void);
extern void picoxcell_map_io(void);

#endif /* __ASM_ARCH_PICOXCELL_CORE_H__ */

/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#ifndef __PICOXCELL_COMMON_H__
#define __PICOXCELL_COMMON_H__

#include <asm/mach/time.h>

extern struct sys_timer picoxcell_timer;
extern void picoxcell_map_io(void);
extern void picoxcell_scan_clocks(void);
extern void picoxcell_disable_unused_clks(void);
extern void picoxcell_enable_clks_for_reset(void);

#endif /* __PICOXCELL_COMMON_H__ */

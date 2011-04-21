/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __MACB_PDATA_H__
#define __MACB_PDATA_H__

enum macb_quirks {
	MACB_QUIRK_NO_UNALIGNED_TX = (1 << 1),
	MACB_QUIRK_FORCE_DBW64 = (1 << 2),
	MACB_QUIRK_HAVE_TSU = (1 << 3),
	MACB_QUIRK_HAVE_TSU_CLK = (1 << 4),
};

struct macb_platform_data {
	u32		phy_mask;
	u8		phy_irq_pin;	/* PHY IRQ */
	u8		is_rmii;	/* using RMII interface? */
	unsigned long	quirks;
};

#endif /* __MACB_PDATA_H__ */

/*
 * NAND flash controller device driver platform data.
 * Copyright © 2011, Picochip
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef __DENALI_PDATA_H__
#define __DENALI_PDATA_H__

struct denali_nand_pdata {
	int	nr_ecc_bits;
};

#endif /* __DENALI_PDATA_H__ */

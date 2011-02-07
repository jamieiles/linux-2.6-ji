/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PICOXCELL_GPIO_H
#define PICOXCELL_GPIO_H

#define GPIO_SW_PORT_A_DR_REG_OFFSET	    0x00
#define GPIO_SW_PORT_A_DDR_REG_OFFSET	    0x04
#define GPIO_SW_PORT_A_CTL_REG_OFFSET	    0x08
#define GPIO_SW_PORT_B_DR_REG_OFFSET	    0x0C
#define GPIO_SW_PORT_B_DDR_REG_OFFSET	    0x10
#define GPIO_SW_PORT_B_CTL_REG_OFFSET	    0x14
#define GPIO_SW_PORT_C_DR_REG_OFFSET	    0x18
#define GPIO_SW_PORT_C_DDR_REG_OFFSET	    0x1C
#define GPIO_SW_PORT_C_CTL_REG_OFFSET	    0x20
#define GPIO_SW_PORT_D_DR_REG_OFFSET	    0x24
#define GPIO_SW_PORT_D_DDR_REG_OFFSET	    0x28
#define GPIO_SW_PORT_D_CTL_REG_OFFSET	    0x2C

#define GPIO_INT_EN_REG_OFFSET		    0x30
#define GPIO_INT_MASK_REG_OFFSET	    0x34
#define GPIO_INT_TYPE_LEVEL_REG_OFFSET	    0x38
#define GPIO_INT_POLARITY_REG_OFFSET	    0x3c

#define GPIO_INT_STATUS_REG_OFFSET	    0x40

#define GPIO_PORT_A_EOI_REG_OFFSET	    0x4c
#define GPIO_EXT_PORT_A_REG_OFFSET	    0x50
#define GPIO_EXT_PORT_B_REG_OFFSET	    0x54
#define GPIO_EXT_PORT_C_REG_OFFSET	    0x58
#define GPIO_EXT_PORT_D_REG_OFFSET	    0x5C

#endif /* PICOXCELL_GPIO_H */

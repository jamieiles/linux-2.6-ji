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
#ifndef __PICOXCELL_H__
#define __PICOXCELL_H__

#define BOOT_ROM_BASE			0xFFFF0000
#define BOOT_ROM_SIZE			0x400
#define AXI2PICO_BUFFERS_BASE		0xC0000000
#define AXI2PICO_BUFFERS_SIZE		0x00010000
#define PICOXCELL_PERIPH_BASE		0x80000000
#define PICOXCELL_PERIPH_LENGTH		0x00400000
#define PICOXCELL_MEMIF_BASE		0x80000000
#define PICOXCELL_EBI_BASE		0x80010000
#define PICOXCELL_EMAC_BASE		0x80030000
#define PICOXCELL_DMAC1_BASE		0x80040000
#define PICOXCELL_DMAC2_BASE		0x80050000
#define PICOXCELL_VIC0_BASE		0x80060000
#define PICOXCELL_VIC1_BASE		0x80064000
#define PICOXCELL_TZIC_BASE		0x80068000
#define PICOXCELL_TZPC_BASE		0x80070000
#define PICOXCELL_FUSE_BASE		0x80080000
#define PICOXCELL_SSI_BASE		0x80090000
#define PICOXCELL_AXI2CFG_BASE		0x800A0000
#define PICOXCELL_IPSEC_BASE		0x80100000
#define PICOXCELL_SRTP_BASE		0x80140000
#define PICOXCELL_CIPHER_BASE		0x80180000
#define PICOXCELL_RTCLK_BASE		0x80200000
#define PICOXCELL_TIMER_BASE		0x80210000
#define PICOXCELL_GPIO_BASE		0x80220000
#define PICOXCELL_UART1_BASE		0x80230000
#define PICOXCELL_UART2_BASE		0x80240000
#define PICOXCELL_WDOG_BASE		0x80250000
#define PC30XX_UART3_BASE		0x80270000
#define PC3X3_RNG_BASE			0x800B0000
#define PC30XX_NAND_BASE		0x800C0000
#define PC3X3_TIMER2_BASE		0x80260000
#define PC3X3_OTP_BASE			0xFFFF8000
#define PC30XX_OTP_BASE			0xFFFF8000

#define EBI_CS0_BASE			0x40000000
#define EBI_CS1_BASE			0x48000000
#define EBI_CS2_BASE			0x50000000
#define EBI_CS3_BASE			0x58000000
#define NAND_CS_BASE			0x60000000

#define SRAM_BASE			0x20000000
#define SRAM_START			0x20000000
#define SRAM_SIZE			0x00020000
#define SRAM_VIRT			0xFE400000

#endif /* __PICOXCELL_H__ */

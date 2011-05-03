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

#ifndef PICOXCELL_AXI2CFG_H
#define PICOXCELL_AXI2CFG_H

#define AXI2CFG_SYSCFG_REG_OFFSET		0x0000
#define AXI2CFG_JTAG_ISC_REG_OFFSET		0x0004
#define AXI2CFG_IRQ_REG_OFFSET			0x0008
#define AXI2CFG_PURGE_CFG_PORT_REG_OFFSET	0x000C
#define AXI2CFG_DMA_CFG_REG_OFFSET		0x0010
#define AXI2CFG_DEVICE_ID_REG_OFFSET		0x0014
#define AXI2CFG_REVISION_ID_REG_OFFSET		0x0018
#define AXI2CFG_AXI_ERR_ENABLE_REG_OFFSET	0x001C
#define AXI2CFG_AXI_ERR_CLEAR_REG_OFFSET	0x0020
#define AXI2CFG_AXI_ERR_MASK_REG_OFFSET		0x0024
#define AXI2CFG_AXI_ERR_TEST_REG_OFFSET		0x0028
#define AXI2CFG_AXI_ERR_RAW_REG_OFFSET		0x002C
#define AXI2CFG_AXI_ERR_STATE_REG_OFFSET	0x0030
#define AXI2CFG_CLOCK_GATING_REG_OFFSET		0x0048
#define AXI2CFG_ID_REG_OFFSET			0x0068
#define AXI2CFG_UICC_CFG_REG_OFFSET		0x00C0
#define AXI2CFG_CONFIG_WRITE_REG_OFFSET		0x0100
#define AXI2CFG_CONFIG_READ_REG_OFFSET		0x0200
#define AXI2CFG_DMAC1_CONFIG_REG_OFFSET		0x0300

#define AXI2CFG_SYSCFG_PA_RST_IDX		30
#define AXI2CFG_SYSCFG_SD_ARM_GPIO_SEL_SZ	8
#define AXI2CFG_SYSCFG_SD_ARM_GPIO_SEL_HI	23
#define AXI2CFG_SYSCFG_SD_ARM_GPIO_SEL_LO	16
#define AXI2CFG_SYSCFG_RW_EBI_CLK_DISABLE_IDX	15
#define AXI2CFG_SYSCFG_RW_EXCVEC_EN_IDX		14
#define AXI2CFG_SYSCFG_RW_RMII_EN_IDX		13
#define AXI2CFG_SYSCFG_RW_REVMII_EN_IDX		12
#define AXI2CFG_SYSCFG_SSI_EBI_SEL_SZ		4
#define AXI2CFG_SYSCFG_SSI_EBI_SEL_HI		11
#define AXI2CFG_SYSCFG_SSI_EBI_SEL_LO		8
#define AXI2CFG_SYSCFG_FREQ_SYNTH_MUX_IDX	7
#define AXI2CFG_SYSCFG_MASK_AXI_ERR_IDX		6
#define AXI2CFG_SYSCFG_RW_REMAP_IDX		5
#define AXI2CFG_SYSCFG_WDG_PAUSE_IDX		4
#define AXI2CFG_SYSCFG_CP15DISABLE_IDX		3
#define AXI2CFG_SYSCFG_DMAC1_CH7_IDX		2
#define AXI2CFG_SYSCFG_BOOT_MODE_SZ		2
#define AXI2CFG_SYSCFG_BOOT_MODE_HI		1
#define AXI2CFG_SYSCFG_BOOT_MODE_LO		0

#define AXI2CFG_SYSCFG_PA_RST_MASK \
	(1 << AXI2CFG_SYSCFG_PA_RST_IDX)
#define AXI2CFG_SYSCFG_SD_ARM_GPIO_MASK	\
	(((1 << AXI2CFG_SYSCFG_SD_ARM_GPIO_SEL_SZ) - 1) << \
	 AXI2CFG_SYSCFG_SD_ARM_GPIO_SEL_LO)
#define AXI2CFG_SYSCFG_RW_EXCVEC_EN_MASK \
	(1 << AXI2CFG_SYSCFG_RW_EXCVEC_EN_IDX)
#define AXI2CFG_SYSCFG_RW_RMII_EN_MASK \
	(1 << AXI2CFG_SYSCFG_RW_RMII_EN_IDX)
#define AXI2CFG_SYSCFG_RW_REVMII_EN_MASK \
	(1 << AXI2CFG_SYSCFG_RW_REVMII_EN_IDX)
#define AXI2CFG_SYSCFG_SSI_EBI_SEL_MASK	\
	(((1 << AXI2CFG_SYSCFG_SSI_EBI_SEL_SZ) - 1) << \
	 AXI2CFG_SYSCFG_SSI_EBI_SEL_LO)
#define AXI2CFG_SYSCFG_FREQ_SYNTH_MUX_MASK \
	(1 << AXI2CFG_SYSCFG_FREQ_SYNTH_MUX_IDX)
#define AXI2CFG_SYSCFG_MASK_AXI_ERR_MASK \
	(1 << AXI2CFG_SYSCFG_MASK_AXI_ERR_IDX)
#define AXI2CFG_SYSCFG_RW_REMAP_MASK \
	(1 << AXI2CFG_SYSCFG_RW_REMAP_IDX)
#define AXI2CFG_SYSCFG_WDG_PAUSE_MASK \
	(1 << AXI2CFG_SYSCFG_WDG_PAUSE_IDX)
#define AXI2CFG_SYSCFG_CP15DISABLE_MASK	\
	(1 << AXI2CFG_SYSCFG_CP15DISABLE_IDX)
#define AXI2CFG_SYSCFG_DMAC1_CH7_MASK \
	(1 << AXI2CFG_SYSCFG_DMAC1_CH7_IDX)
#define AXI2CFG_SYSCFG_BOOT_MODE_MASK \
	(((1 << AXI2CFG_SYSCFG_BOOT_MODE_SZ) - 1) << \
	 AXI2CFG_SYSCFG_BOOT_MODE_LO)

#define AXI2CFG_AXI_RD_ERR_MASK			   0x00000FFF
#define AXI2CFG_AXI_WR_ERR_MASK			   0x00FFF000
#define AXI2CFG_AXI_ERR_MASK_NONE		   0
#define AXI2CFG_AXI_ERR_ENABLE_ALL		   0x00FFFFFF

#ifndef __ASSEMBLY__

/*
 * axi2cfg_config_read - Read a number of 16 bit words from a picoArray axi2cfg.
 *
 * Returns the number of words read on success, negative errno on failure.
 *
 * @axi2cfg_base: The base address of the upper axi2cfg.
 * @aeid: The CAEID of the AE to read from.
 * @ae_addr: The address to begin reading from within the AE.
 * @buf: The buffer to store the results in.
 * @count: The number of 16 bit words to read.
 */
extern int axi2cfg_config_read(u16 aeid, u16 ae_addr, u16 *buf, u16 count);

/*
 * axi2cfg_config_write - Write a number of 16 bit words to a picoArray axi2cfg.
 *
 * @axi2cfg_base: The base address of the upper axi2cfg.
 * @aeid: The CAEID of the AE to write to.
 * @ae_addr: The address to begin writing to within the AE.
 * @buf: The buffer to read the words from.
 * @count: The number of 16 bit words to write.
 */
extern void axi2cfg_config_write(u16 aeid, u16 ae_addr, const u16 *buf,
				 u16 count);

/*
 * ax2cfg_write_buf - Write a series of configuration words to the AXI2CFG
 *	config write port.
 *
 * @buf: The buffer to write.
 * @nr_words: The number of 32 bit words to write.
 */
extern void axi2cfg_write_buf(const u32 *buf, unsigned nr_words);

/*
 * axi2cfg_init - initialize the AXI2CFG hardware.
 */
extern void axi2cfg_init(void);

/*
 * axi2cfg_readl - read a register in the axi2cfg.
 *
 * Returns the value of the register.
 *
 * @offs: the byte offset to read from.
 */
extern unsigned long axi2cfg_readl(unsigned long offs);

/*
 * axi2cfg_writel - write an axi2cfg AXI domain register.
 *
 * @val: the value to write.
 * @offs: the byte offset to write to.
 */
extern void axi2cfg_writel(unsigned long val, unsigned long offs);

#endif /* !__ASSEMBLY__ */

#endif /* PICOXCELL_AXI2CFG_H */

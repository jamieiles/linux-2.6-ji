/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This file implements functions for using the axi2cfg to configure and debug
 * picoArray systems providing configuration bus access over the axi2cfg.
 */
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include <mach/hardware.h>

/* Configuration port write bit positions. */
#define CAEID_BIT_MASK	    (1 << 19)	/* AE ID signal. */
#define CADDR_BIT_MASK	    (1 << 18)	/* AE ADDR signal. */
#define CREAD_BIT_MASK	    (1 << 17)	/* READ data signal. */
#define CWRITE_BIT_MASK     (1 << 16)	/* WRITE data signal. */

#define RB_FAIL_MASK	    (1 << 17)	/* Readback failed. */
#define RB_VALID_MASK	    (1 << 16)	/* Readback valid. */

#define NR_RETRIES	    16		/* The number of retries for an
					 * AXI2Cfg config read. */

static DEFINE_SPINLOCK(axi2cfg_lock);
static void __iomem *axi2cfg;

#define CFG_WRITE_PORT	    0x100	/* Write port offset. */
#define CFG_READ_PORT	    0x200	/* Read port offset. */

int axi2cfg_config_read(u16 aeid, u16 ae_addr, u16 *buf, u16 count)
{
	u32 val;
	void __iomem *write_p = axi2cfg + CFG_WRITE_PORT;
	void __iomem *read_p = axi2cfg + CFG_READ_PORT;
	u16 rc, to_read = count;
	unsigned i, retries;
	unsigned long flags;

	spin_lock_irqsave(&axi2cfg_lock, flags);

	val = aeid | CAEID_BIT_MASK;
	writel(val, write_p);

	while (to_read) {
		/* Output the address to read from. */
		val = (ae_addr + (count - to_read)) | CADDR_BIT_MASK;
		writel(val, write_p);

		/* Dispatch the read requests. We have a 64 entry FIFO. */
		rc = min_t(u16, to_read, 64);
		val = CREAD_BIT_MASK | rc;
		writel(val, write_p);

		/* Now read the values. */
		for (i = 0; i < rc; ++i) {
			retries = NR_RETRIES;
			while (retries) {
				val = readl(read_p);
				if (val & (RB_VALID_MASK | RB_FAIL_MASK))
					break;
				--retries;
				cpu_relax();
			}

			if (!retries || (val & RB_FAIL_MASK)) {
				pr_warning("config read %04x@%04x failed\n",
					   aeid,
					   (ae_addr + (count - to_read) + i));
				break;
			} else
				buf[(count - to_read) + i] = val & 0xFFFF;
		}

		if (val & RB_FAIL_MASK)
			break;

		to_read -= rc;
	}

	spin_unlock_irqrestore(&axi2cfg_lock, flags);

	return !(val & RB_FAIL_MASK) ? count : -EIO;
}
EXPORT_SYMBOL_GPL(axi2cfg_config_read);

void axi2cfg_config_write(u16 aeid, u16 ae_addr, const u16 *buf, u16 count)
{
	u32 val;
	void __iomem *write_p = axi2cfg + CFG_WRITE_PORT;
	unsigned i;
	unsigned long flags;

	spin_lock_irqsave(&axi2cfg_lock, flags);

	/* Output the AEID to read from. */
	val = aeid | CAEID_BIT_MASK;
	writel(val, write_p);

	/* Output the address to read from. */
	val = ae_addr | CADDR_BIT_MASK;
	writel(val, write_p);

	for (i = 0; i < count; ++i) {
		val = buf[i] | CWRITE_BIT_MASK;
		writel(val, write_p);
	}

	spin_unlock_irqrestore(&axi2cfg_lock, flags);
}
EXPORT_SYMBOL_GPL(axi2cfg_config_write);

void axi2cfg_write_buf(const u32 *buf, unsigned nr_words)
{
	void __iomem *write_p = axi2cfg + CFG_WRITE_PORT;
	unsigned i;
	unsigned long flags;

	spin_lock_irqsave(&axi2cfg_lock, flags);

	for (i = 0; i < nr_words; ++i)
		writel(*buf++, write_p);

	spin_unlock_irqrestore(&axi2cfg_lock, flags);
}
EXPORT_SYMBOL_GPL(axi2cfg_write_buf);

unsigned long axi2cfg_readl(unsigned long offs)
{
	return readl(axi2cfg + offs);
}

void axi2cfg_writel(unsigned long val, unsigned long offs)
{
	writel(val, axi2cfg + offs);
}
EXPORT_SYMBOL_GPL(axi2cfg_writel);

void __init axi2cfg_init(void)
{
	axi2cfg = ioremap(PICOXCELL_AXI2CFG_BASE, 0x300);
	BUG_ON(!axi2cfg);
}
EXPORT_SYMBOL_GPL(axi2cfg_readl);

/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 *
 * This driver implements a picoxcellotp backend for reading and writing the
 * OTP memory in Picochip PC3X3 devices. This OTP can be used for executing
 * secure boot code or for the secure storage of keys and any other user data.
 */
#define pr_fmt(fmt) "pc3x3otp: " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/otp.h>
#include <linux/platform_device.h>

/*
 * To test the user interface and most of the driver logic, we have a test
 * mode whereby rather than writing to OTP we have a RAM buffer that simulates
 * the OTP. This means that we can test everything apart from:
 *
 *	- The OTP state machines and commands.
 *	- Failure to program bits.
 */
static int test_mode;
module_param(test_mode, bool, S_IRUSR);
MODULE_PARM_DESC(test_mode,
		 "Run in test mode (use a memory buffer rather than OTP");


/* The control and status registers follow the AXI OTP map. */
#define OTP_CTRL_BASE			0x4000

/*
 * This is the maximum number of times to try and soak a failed bit. We get
 * this from the Sidense documentation. After 16 attempts it is very unlikely
 * that anything will change.
 */
#define MAX_PROGRAM_RETRIES			16
#define OTP_MACRO_CMD_REG_OFFSET		0x00
#define OTP_MACRO_STATUS_REG_OFFSET		0x04
#define OTP_MACRO_CONFIG_REG_OFFSET		0x08
#define OTP_MACRO_ADDR_REG_OFFSET		0x0C
#define OTP_MACRO_D_LO_REG_OFFSET		0x10
#define OTP_MACRO_D_HI_REG_OFFSET		0x14
#define OTP_MACRO_Q_LO_REG_OFFSET		0x20
#define OTP_MACRO_Q_HI_REG_OFFSET		0x24
#define OTP_MACRO_Q_MR_REG_OFFSET		0x28
#define OTP_MACRO_Q_MRAB_REG_OFFSET		0x2C
#define OTP_MACRO_Q_SR_LO_REG_OFFSET		0x30
#define OTP_MACRO_Q_SR_HI_REG_OFFSET		0x34
#define OTP_MACRO_Q_RR_LO_REG_OFFSET		0x38
#define OTP_MACRO_Q_RR_HI_REG_OFFSET		0x3C
#define OTP_MACRO_TIME_RD_REG_OFFSET		0x40
#define OTP_MACRO_TIME_WR_REG_OFFSET		0x44
#define OTP_MACRO_TIME_PGM_REG_OFFSET		0x48
#define OTP_MACRO_TIME_PCH_REG_OFFSET		0x4C
#define OTP_MACRO_TIME_CMP_REG_OFFSET		0x50
#define OTP_MACRO_TIME_RST_REG_OFFSET		0x54
#define OTP_MACRO_TIME_PWR_REG_OFFSET		0x58
#define OTP_MACRO_DIRECT_IO_REG_OFFSET		0x5C

/*
 * The OTP addresses of the special register. This is in the boot
 * sector and we use words 0 and 2 of sector 0 in redundant format.
 */
#define SR_ADDRESS_0				((1 << 11) | 0x0)
#define SR_ADDRESS_2				((1 << 11) | 0x2)
#define SR_AXI_ADDRESS_MASK			0x7

#define OTP_MR_REDUNDANT_READ_MASK		(1 << 4)
#define OTP_MR_DIFFERENTIAL_READ_MASK		(1 << 0)
#define OTP_MRA_CHARGE_PUMP_ENABLE_MASK		(1 << 12)
#define OTP_MRA_CHARGE_PUMP_MONITOR_MASK	(1 << 15)
#define OTP_MRA_READ_REFERENCE_LEVEL9_MASK	(1 << 9)
#define OTP_MRA_READ_REFERENCE_LEVEL5_MASK	(1 << 5)
#define OTP_STATUS_VPP_APPLIED			(1 << 4)
#define OTP_TIME_PGM_PULSE_MASK			0x7FF
#define OTP_STATUS_LCS				(1 << 1)

#define OTP_MR_SELF_TIMING			(1 << 2)
#define OTP_MR_PROGRAMMABLE_DELAY		(1 << 5)
#define OTP_MR_PROGRAMMABLE_DELAY_CONTROL	(1 << 8)

#define OTP_MRB_VREF_ADJUST_0			(1 << 0)
#define OTP_MRB_VREF_ADJUST_1			(1 << 1)
#define OTP_MRB_VREF_ADJUST_3			(1 << 3)
#define OTP_MRB_READ_TIMER_DELAY_CONTROL	(1 << 12)

/*
 * Programming pulse times. For the normal pulse, we use a programming time of
 * 51.2uS. For a soak pulse where bits fail to program we use a 1mS pulse.
 */
#define OTP_NORMAL_PGM_PULSE_LENGTH		0x50
#define OTP_SOAK_PGM_PULSE_LENGTH		0x61B

enum otp_command {
	OTP_COMMAND_IDLE,
	OTP_COMMAND_WRITE,
	OTP_COMMAND_PROGRAM,
	OTP_COMMAND_READ,
	OTP_COMMAND_WRITE_MR,
	OTP_COMMAND_PRECHARGE,
	OTP_COMMAND_COMPARE,
	OTP_COMMAND_RESET,
	OTP_COMMAND_RESET_M,
	OTP_COMMAND_POWER_DOWN,
	OTP_COMMAND_AUX_UPDATE_A,
	OTP_COMMAND_AUX_UPDATE_B,
	OTP_COMMAND_WRITE_PROGRAM,
	OTP_COMMAND_WRITE_MRA,
	OTP_COMMAND_WRITE_MRB,
	OTP_COMMAND_RESET_MR,
};

#define PC3X3_OTP_WORD_SIZE		8

/*
 * The number of words in the OTP device. The device is 16K bytes and the word
 * size is 64 bits.
 */
#define OTP_NUM_WORDS	    (SZ_16K / PC3X3_OTP_WORD_SIZE)

/*
 * The OTP device representation. We can have a static structure as there is
 * only ever one OTP device in a system.
 *
 * @iomem: the io memory for the device that should be accessed with the I/O
 *	accessors.
 * @mem: the 16KB of OTP memory that can be accessed like normal memory. When
 *	we probe, we force the __iomem away so we can read it directly.
 * @test_mode_sr0, test_mode_sr2 the values of the special register when we're
 *	in test mode.
 */
struct pc3x3_otp {
	struct otp_device   *dev;
	void __iomem	    *iomem;
	void		    *mem;
	struct clk	    *clk;
	u64		    test_mode_sr0, test_mode_sr2;
	unsigned long	    registered_regions;
};

static int pc3x3_otp_register_regions(struct pc3x3_otp *dev,
				      bool need_unlocked);

static inline void pc3x3_otp_write_reg(struct pc3x3_otp *otp, unsigned reg_num,
				       u32 value)
{
	writel(value, otp->iomem + OTP_CTRL_BASE + reg_num);
}

static inline u32 pc3x3_otp_read_reg(struct pc3x3_otp *otp, unsigned reg_num)
{
	return readl(otp->iomem + OTP_CTRL_BASE + reg_num);
}

static inline u32 pc3x3_otp_read_sr(struct pc3x3_otp *otp)
{
	if (test_mode)
		return otp->test_mode_sr0 | otp->test_mode_sr2;

	return pc3x3_otp_read_reg(otp, OTP_MACRO_Q_SR_LO_REG_OFFSET);
}

/*
 * Get the region format. The region format encoding and number of regions are
 * encoded in the bottom 32 bis of the special register:
 *
 *  20: enable redundancy replacement.
 *  [2:0]: AXI address mask - determines the number of address bits to use for
 *  selecting the region to read from.
 *  [m:n]: the format for region X where n := (X * 2) + 4 and m := n + 1.
 */
static enum otp_redundancy_fmt
__pc3x3_otp_region_get_fmt(struct pc3x3_otp *otp,
			   const struct otp_region *region)
{
	unsigned shift = (region->region_nr * 2) + 4;

	return (pc3x3_otp_read_sr(otp) >> shift) & 0x3;
}

static enum otp_redundancy_fmt
pc3x3_otp_region_get_fmt(struct otp_region *region)
{
	struct pc3x3_otp *otp = dev_get_drvdata(region->dev.parent);

	return __pc3x3_otp_region_get_fmt(otp, region);
}

/*
 * Find out how many regions the OTP is partitioned into. This can be 1, 2, 4
 * or 8.
 */
static int pc3x3_otp_num_regions(struct pc3x3_otp *otp)
{
	u32 addr_mask;
	int nr_regions;

	addr_mask = pc3x3_otp_read_sr(otp) & SR_AXI_ADDRESS_MASK;

	switch (addr_mask) {
	case 0:
		nr_regions = 1;
		break;
	case 4:
		nr_regions = 2;
		break;
	case 6:
		nr_regions = 4;
		break;
	case 7:
		nr_regions = 8;
		break;
	default:
		WARN(1, "invalid special register region mask\n");
		nr_regions = -EINVAL;
	}

	return nr_regions;
}

/*
 * Find the byte offset of the first word in the region from the base of the
 * OTP.
 */
static unsigned pc3x3_otp_region_base(struct pc3x3_otp *otp,
				      const struct otp_region *region)
{
	int num_regions = pc3x3_otp_num_regions(otp);
	unsigned real_region_sz = SZ_16K / num_regions;

	return (region->region_nr * real_region_sz) / PC3X3_OTP_WORD_SIZE;
}

static void pc3x3_otp_do_cmd(struct pc3x3_otp *otp, enum otp_command cmd)
{
	pc3x3_otp_write_reg(otp, OTP_MACRO_CMD_REG_OFFSET, cmd);
	wmb();

	/*
	 * If we're talking to OTP then we need to wait for the command to
	 * finish.
	 */
	if (!test_mode)
		while (pc3x3_otp_read_reg(otp, OTP_MACRO_CMD_REG_OFFSET) !=
		       OTP_COMMAND_IDLE)
			cpu_relax();
}

/*
 * Read a word from OTP.
 *
 * @addr the word address to read from.
 * @val the destination to store the value in.
 *
 * Prerequisites: the OTP must be in single-ended read mode so that we can
 * correctly read the raw word.
 */
static int pc3x3_otp_raw_read_word(struct pc3x3_otp *otp, unsigned addr,
				   u64 *val)
{
	if (addr == SR_ADDRESS_0 && test_mode)
		*val = otp->test_mode_sr0;
	else if (addr == SR_ADDRESS_2 && test_mode)
		*val = otp->test_mode_sr2;
	else {
		union {
			u64 d64;
			u32 d32[2];
		} converter;

		pc3x3_otp_write_reg(otp, OTP_MACRO_ADDR_REG_OFFSET, addr);
		pc3x3_otp_do_cmd(otp, OTP_COMMAND_READ);

		converter.d32[0] =
			pc3x3_otp_read_reg(otp, OTP_MACRO_Q_LO_REG_OFFSET);
		converter.d32[1] =
			pc3x3_otp_read_reg(otp, OTP_MACRO_Q_HI_REG_OFFSET);

		if (!test_mode)
			*val = converter.d64;
		else
			memcpy(val, otp->mem + addr * sizeof(u64), sizeof(u64));
	}

	return 0;
}

/*
 * Set the redundancy mode to a specific format. This only affects the
 * readback through the AXI map and does not store the redundancy format in
 * the special register.
 */
static void __pc3x3_otp_redundancy_mode_set(struct pc3x3_otp *otp,
					    enum otp_redundancy_fmt fmt)
{
	u32 mr_lo = 0;

	if (fmt == OTP_REDUNDANCY_FMT_REDUNDANT)
		mr_lo |= OTP_MR_REDUNDANT_READ_MASK;
	else if (fmt == OTP_REDUNDANCY_FMT_DIFFERENTIAL)
		mr_lo |= OTP_MR_DIFFERENTIAL_READ_MASK;
	else if (fmt == OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT)
		mr_lo |= OTP_MR_REDUNDANT_READ_MASK |
			 OTP_MR_DIFFERENTIAL_READ_MASK;

	/* Load the data register with the new MR contents. */
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_LO_REG_OFFSET, mr_lo);
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_HI_REG_OFFSET, 0);

	/* Write the MR and wait for the write to complete. */
	pc3x3_otp_do_cmd(otp, OTP_COMMAND_WRITE_MR);
}

static int pc3x3_otp_redundancy_mode_set(struct otp_device *dev,
					 enum otp_redundancy_fmt fmt)
{
	struct pc3x3_otp *otp = dev_get_drvdata(&dev->dev);

	__pc3x3_otp_redundancy_mode_set(otp, fmt);

	return 0;
}

#ifdef CONFIG_OTP_WRITE_ENABLE
static void pc3x3_otp_write_MR(struct pc3x3_otp *otp, u32 value)
{
	/* Load the data register with the new contents. */
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_LO_REG_OFFSET, value);
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_HI_REG_OFFSET, 0);

	/* Write the register and wait for the write to complete. */
	pc3x3_otp_do_cmd(otp, OTP_COMMAND_WRITE_MR);
}

/*
 * Create a write function for a given OTP auxillary mode register. This
 * writes the auxillary mode register through the mode register then restores
 * the contents of the mode register.
 */
#define OTP_REG_WRITE_FUNCTIONS(_name)					    \
static void pc3x3_otp_write_##_name(struct pc3x3_otp *otp, u32 value)	    \
{									    \
	u32 mr = pc3x3_otp_read_reg(otp, OTP_MACRO_Q_MR_REG_OFFSET);	    \
									    \
	/* Load the data register with the new contents. */		    \
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_LO_REG_OFFSET, value);	    \
	pc3x3_otp_write_reg(otp, OTP_MACRO_D_HI_REG_OFFSET, 0);		    \
									    \
	/* Write the register and wait for the write to complete. */	    \
	pc3x3_otp_do_cmd(otp, OTP_COMMAND_WRITE_##_name);		    \
									    \
	/* Restore the original value of the MR. */			    \
	pc3x3_otp_write_MR(otp, mr);					    \
}

OTP_REG_WRITE_FUNCTIONS(MRA);
OTP_REG_WRITE_FUNCTIONS(MRB);

/*
 * Enable the charge pump. This monitors the VPP voltage and waits for it to
 * reach the correct programming level.
 *
 * @enable set to non-zero to enable the charge pump, zero to disable it.
 */
static void pc3x3_otp_charge_pump_enable(struct pc3x3_otp *otp, int enable)
{
	u32 mra = enable ?
		(OTP_MRA_CHARGE_PUMP_ENABLE_MASK |
		 OTP_MRA_CHARGE_PUMP_MONITOR_MASK |
		 OTP_MRA_READ_REFERENCE_LEVEL9_MASK |
		 OTP_MRA_READ_REFERENCE_LEVEL5_MASK) : 0;

	pc3x3_otp_write_MRA(otp, mra);

	/* Now wait for VPP to reach the correct level. */
	if (enable && !test_mode) {
		while (!(pc3x3_otp_read_reg(otp, OTP_MACRO_STATUS_REG_OFFSET) &
			 OTP_STATUS_VPP_APPLIED))
			cpu_relax();
	}

	udelay(1);
}

/*
 * Program a word of OTP to a raw address. This will program an absolute value
 * into the OTP so if the current word needs to be modified then this needs to
 * be done with a read-modify-write cycle with the read-modify handled above.
 *
 * The actual write operation can't fail here but we don't do any verification
 * to make sure that the correct data got written. That must be handled by the
 * layer above.
 */
static void pc3x3_otp_raw_program_word(struct pc3x3_otp *otp, unsigned addr,
				       u64 v)
{
	unsigned bit_offs;
	u64 tmp;
	int set_to_program = addr & 1 ? 0 : 1;

	if (test_mode) {
		if (addr != SR_ADDRESS_0 && addr != SR_ADDRESS_2) {
			u64 old;

			if (pc3x3_otp_raw_read_word(otp, addr, &old))
				return;

			v = (addr & 1) ? old & ~v : old | v;

			memcpy(otp->mem + (addr * PC3X3_OTP_WORD_SIZE), &v,
			       sizeof(v));
		} else {
			/*
			 * The special register OTP values are stored in the
			 * boot rows that live outside of the 16KB of normal
			 * OTP so we can't address them directly.
			 */
			if (addr == SR_ADDRESS_0)
				otp->test_mode_sr0 |= v;
			else
				otp->test_mode_sr2 |= v;
		}
	}

	/* Set the address of the word that we're writing. */
	pc3x3_otp_write_reg(otp, OTP_MACRO_ADDR_REG_OFFSET, addr);

	for (bit_offs = 0; v && bit_offs < 64; ++bit_offs, v >>= 1) {
		if (!(v & 0x1))
			continue;

		tmp = set_to_program ? ~(1LLU << bit_offs) :
			(1LLU << bit_offs);
		pc3x3_otp_write_reg(otp, OTP_MACRO_D_LO_REG_OFFSET,
				(u32)tmp & 0xFFFFFFFF);
		pc3x3_otp_write_reg(otp, OTP_MACRO_D_HI_REG_OFFSET,
				(u32)(tmp >> 32) & 0xFFFFFFFF);

		/* Start programming the bit and wait for it to complete. */
		pc3x3_otp_do_cmd(otp, OTP_COMMAND_WRITE_PROGRAM);
	}
}

static inline void pc3x3_otp_set_program_pulse_len(struct pc3x3_otp *otp,
						   unsigned len)
{
	u32 v = pc3x3_otp_read_reg(otp, OTP_MACRO_TIME_PGM_REG_OFFSET);
	v &= ~OTP_TIME_PGM_PULSE_MASK;
	v |= len;
	pc3x3_otp_write_reg(otp, OTP_MACRO_TIME_PGM_REG_OFFSET, v);
}

/*
 * Write a raw word in OTP. This will program a word into OTP memory and do
 * any read-modify-write that is necessary. For example if address 0 contains
 * 0x00ef, then writing 0xbe00 will result in address 0 containing 0xbeef.
 * This does not handle redundancy - this should be done at a higher level.
 *
 * @addr the word address to write to.
 * @val the value to program into the OTP.
 *
 * Prerequisites: the OTP must be in single-ended read mode so that we can
 * correctly verify the word.
 */
static int pc3x3_otp_raw_write_word(struct pc3x3_otp *otp, unsigned addr,
				    u64 val)
{
	/*
	 * We program even addresses by setting 0 bits to one and programm odd
	 * addresses by clearing 1 bits to 0.
	 */
	int set_to_program = addr & 1 ? 0 : 1;
	int retries = 0, err = 0;
	u64 orig, v;

	if (pc3x3_otp_raw_read_word(otp, addr, &orig))
		return -EIO;

	v = set_to_program ? val & ~orig : ~val & orig;

	/*
	 * Enable the charge pump and configure initial timing to begin
	 * programming.
	 */
	pc3x3_otp_charge_pump_enable(otp, 1);
	pc3x3_otp_write_MRB(otp, OTP_MRB_VREF_ADJUST_3 |
				 OTP_MRB_READ_TIMER_DELAY_CONTROL);
	pc3x3_otp_write_MR(otp, OTP_MR_SELF_TIMING |
				OTP_MR_PROGRAMMABLE_DELAY |
				OTP_MR_PROGRAMMABLE_DELAY_CONTROL);
	pc3x3_otp_raw_program_word(otp, addr, v);
	udelay(1);

	while (retries < MAX_PROGRAM_RETRIES) {
		/* Update orig so we only reprogram the unprogrammed bits. */
		if (pc3x3_otp_raw_read_word(otp, addr, &orig)) {
			err = -EIO;
			break;
		}

		/* If we've programmed correctly we have nothing else to do. */
		if (val == orig) {
			err = 0;
			break;
		}

		/* Reset the mode register. */
		pc3x3_otp_write_MRB(otp, OTP_MRB_VREF_ADJUST_0 |
					 OTP_MRB_VREF_ADJUST_1 |
					 OTP_MRB_VREF_ADJUST_3 |
					 OTP_MRB_READ_TIMER_DELAY_CONTROL);
		pc3x3_otp_do_cmd(otp, OTP_COMMAND_RESET_MR);

		/* Increase the programming pulse length. */
		pc3x3_otp_set_program_pulse_len(otp, OTP_SOAK_PGM_PULSE_LENGTH);

		/* Work out the failed bits. */
		v = set_to_program ? val & ~orig : ~val & orig;
		pc3x3_otp_raw_program_word(otp, addr, v);

		/* Restore the programming pulse length. */
		pc3x3_otp_set_program_pulse_len(otp,
						OTP_NORMAL_PGM_PULSE_LENGTH);

		/* Update orig so we only reprogram the unprogrammed bits. */
		if (pc3x3_otp_raw_read_word(otp, addr, &orig)) {
			err = -EIO;
			break;
		}

		/* If we've programmed correctly we have nothing else to do. */
		if (val == orig) {
			err = 0;
			break;
		}

		pc3x3_otp_write_MRB(otp, OTP_MRB_VREF_ADJUST_3 |
					 OTP_MRB_READ_TIMER_DELAY_CONTROL);
		pc3x3_otp_write_MR(otp, OTP_MR_SELF_TIMING |
					OTP_MR_PROGRAMMABLE_DELAY |
					OTP_MR_PROGRAMMABLE_DELAY_CONTROL);
		udelay(1);
		++retries;
	}

	/* Disable the charge pump. We're done now. */
	pc3x3_otp_charge_pump_enable(otp, 0);
	pc3x3_otp_write_MRB(otp, 0);
	pc3x3_otp_write_MRA(otp, 0);
	pc3x3_otp_do_cmd(otp, OTP_COMMAND_RESET_MR);

	if (!err && retries >= MAX_PROGRAM_RETRIES) {
		dev_warn(&otp->dev->dev,
			 "writing to raw address %x failed to program after %d attempts\n",
			 addr, MAX_PROGRAM_RETRIES);
		err = -EBADMSG;
	}

	return err;
}

/*
 * Write a data word to an OTP region. The value will be used in a
 * read-modify-write cycle to ensure that bits can't be flipped if they have
 * already programmed (the hardware isn't capable of this). This also takes
 * into account the redundancy addressing and formatting.
 */
static int pc3x3_otp_write_word(struct otp_device *otp_dev,
				struct otp_region *region, unsigned long addr,
				u64 word)
{
	struct pc3x3_otp *otp = otp_dev_get_drvdata(otp_dev);
	enum otp_redundancy_fmt fmt = __pc3x3_otp_region_get_fmt(otp, region);
	unsigned i, num_words, raw_addresses[4];
	u64 result;
	int err = 0;

	/* Enter the single-ended read mode. */
	__pc3x3_otp_redundancy_mode_set(otp, OTP_REDUNDANCY_FMT_SINGLE_ENDED);

	/*
	 * Work out what raw addresses and values we need to write into the
	 * OTP to make sure that the value we want gets read back out
	 * correctly.
	 */
	switch (fmt) {
	case OTP_REDUNDANCY_FMT_SINGLE_ENDED:
		num_words	    = 1;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) + addr;
		break;

	case OTP_REDUNDANCY_FMT_REDUNDANT:
		num_words	    = 2;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFE) << 1) | (addr & 1));
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFE) << 1) | (addr & 1) | 2);
		break;

	case OTP_REDUNDANCY_FMT_DIFFERENTIAL:
		num_words	    = 2;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 1));
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 1) | 1);
		break;

	case OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT:
		num_words	    = 4;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      ((addr & 0xFFFF) << 2);
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x1);
		raw_addresses[2]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x2);
		raw_addresses[3]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x3);
		break;

	default:
		err = -EINVAL;
		goto out;
	}

	/*
	 * Verify the raw words. If we are doing strict programming then they
	 * must all program correctly. If we aren't doing strict programming
	 * then allow failures to 'slip through' for now. If the word can be
	 * read back correctly in the redundant mode then that's fine with the
	 * user.
	 */
	for (i = 0; i < num_words; ++i)
		err = pc3x3_otp_raw_write_word(otp, raw_addresses[i], word);
		if (err && otp_strict_programming_enabled(otp->dev))
			goto out;

	/* Go back to the real redundancy mode and verify the whole word. */
	__pc3x3_otp_redundancy_mode_set(otp, fmt);

	if (otp_dev->ops->read_word(otp_dev, region, addr, &result)) {
		err = -EIO;
		goto out;
	}

	/*
	 * Now check that the word has been correctly programmed with the
	 * region formatting.
	 */
	if (result == word) {
		err = 0;
	} else {
		dev_warn(&region->dev,
			 "word at address %lx write failed %llx != %llx (result != expected)\n",
			 addr, result, word);
		err = -EBADMSG;
	}

out:
	return err;
}

/*
 * Write the special register. In PC3X3, we only use the lower 32 bits of the
 * SR to indicate the partitioning and the region formats so we do a
 * read-modify-write of the whole 64 bit value.
 */
static int pc3x3_otp_write_sr(struct pc3x3_otp *otp, u32 sr_lo)
{
	if (pc3x3_otp_raw_write_word(otp, SR_ADDRESS_0, sr_lo)) {
		dev_warn(&otp->dev->dev,
			 "failed to write special register (word 0)\n");
		return -EIO;
	}

	if (pc3x3_otp_raw_write_word(otp, SR_ADDRESS_2, sr_lo)) {
		dev_warn(&otp->dev->dev,
			 "failed to write special register (word 0)\n");
		return -EIO;
	}

	/*
	 * Reset the OTP so that when we read the special register again we
	 * get the value that we've just written.
	 */
	pc3x3_otp_do_cmd(otp, OTP_COMMAND_RESET);

	return 0;
}

static int pc3x3_otp_region_set_fmt(struct otp_region *region,
				    enum otp_redundancy_fmt new_fmt)
{
	int err;
	struct pc3x3_otp *otp = dev_get_drvdata(region->dev.parent);
	enum otp_redundancy_fmt fmt = __pc3x3_otp_region_get_fmt(otp, region);
	unsigned shift = (region->region_nr * 2) + 4;
	unsigned long sr;

	/*
	 * We can't clear format bits so we can only do certain transitions.
	 * It is possible to go from redundant to differential-redundant or
	 * differential to differential redundant but if the region is already
	 * programmed this could give unexpected results. However, the user
	 * _might_ know what they're doing.
	 */
	if (fmt & ~new_fmt) {
		err = -EINVAL;
		goto out;
	}
	if (fmt == new_fmt) {
		err = 0;
		goto out;
	}

	sr = pc3x3_otp_read_sr(otp);
	sr |= new_fmt << shift;
	err = pc3x3_otp_write_sr(otp, sr);

out:
	return err;
}

static int pc3x3_otp_set_nr_regions(struct otp_device *dev, int nr_regions)
{
	struct pc3x3_otp *otp = dev_get_drvdata(&dev->dev);
	unsigned long sr = pc3x3_otp_read_sr(otp);
	u32 new_mask, addr_mask = sr & SR_AXI_ADDRESS_MASK;
	int err = 0;

	switch (nr_regions) {
	case 1:
		new_mask = 0;
		break;
	case 2:
		new_mask = 4;
		break;
	case 4:
		new_mask = 6;
		break;
	case 8:
		new_mask = 7;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Check that we aren't trying to clear any bits and reduce the number
	 * of regions. This is OTP so we can only increase.
	 */
	if (addr_mask & ~new_mask)
		return -EINVAL;

	if (addr_mask == new_mask)
		return 0;

	err = pc3x3_otp_write_sr(otp, sr | new_mask);
	if (err)
		return err;

	return pc3x3_otp_register_regions(otp, true);
}
#else /* CONFIG_OTP_WRITE_ENABLE */
#define pc3x3_otp_region_set_fmt	NULL
#define pc3x3_otp_write_word		NULL
#define pc3x3_otp_set_nr_regions	NULL
#endif /* CONFIG_OTP_WRITE_ENABLE */

/*
 * Read a word from a specificied OTP region. The address is the user address
 * for the word to be read and should not take the redundancy into account.
 */
static int pc3x3_otp_read_word(struct otp_device *otp_dev,
			       struct otp_region *region, unsigned long addr,
			       u64 *word)
{
	struct pc3x3_otp *otp = otp_dev_get_drvdata(otp_dev);
	enum otp_redundancy_fmt fmt = __pc3x3_otp_region_get_fmt(otp, region);
	unsigned num_words, raw_addresses[4];
	u64 result = 0, raw_values[4];
	int err = 0;

	/* Enter the single-ended read mode. */
	__pc3x3_otp_redundancy_mode_set(otp, OTP_REDUNDANCY_FMT_SINGLE_ENDED);

	/*
	 * If we're running with real OTP then the read is simple, just copy
	 * it from the AXI map.
	 */
	if (!test_mode) {
		memcpy(word,
		       otp->mem + (pc3x3_otp_region_base(otp, region) + addr) *
		       PC3X3_OTP_WORD_SIZE, sizeof(*word));
		return 0;
	}

	/*
	 * If we're in test mode then this is slightly more complicated. We
	 * need to decode the address into the raw address(s) that the block
	 * uses and handle the redundancy format. This allows us to test that
	 * we've programmed all of the redundant words in the correct format.
	 */
	switch (fmt) {
	case OTP_REDUNDANCY_FMT_SINGLE_ENDED:
		num_words	    = 1;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) + addr;
		pc3x3_otp_raw_read_word(otp, raw_addresses[0], &raw_values[0]);
		result = raw_values[0];
		break;

	case OTP_REDUNDANCY_FMT_REDUNDANT:
		num_words	    = 2;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFE) << 1) | (addr & 1));
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFE) << 1) | (addr & 1) | 2);
		pc3x3_otp_raw_read_word(otp, raw_addresses[0], &raw_values[0]);
		pc3x3_otp_raw_read_word(otp, raw_addresses[1], &raw_values[1]);
		result = raw_values[0] | raw_values[1];
		break;

	case OTP_REDUNDANCY_FMT_DIFFERENTIAL:
		num_words	    = 2;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 1));
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 1) | 1);
		pc3x3_otp_raw_read_word(otp, raw_addresses[0], &raw_values[0]);
		pc3x3_otp_raw_read_word(otp, raw_addresses[1], &raw_values[1]);
		result = raw_values[0] | raw_values[1];
		break;

	case OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT:
		num_words	    = 4;
		raw_addresses[0]    = pc3x3_otp_region_base(otp, region) +
				      ((addr & 0xFFFF) << 2);
		raw_addresses[1]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x1);
		raw_addresses[2]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x2);
		raw_addresses[3]    = pc3x3_otp_region_base(otp, region) +
				      (((addr & 0xFFFF) << 2) | 0x3);
		pc3x3_otp_raw_read_word(otp, raw_addresses[0], &raw_values[0]);
		pc3x3_otp_raw_read_word(otp, raw_addresses[1], &raw_values[1]);
		pc3x3_otp_raw_read_word(otp, raw_addresses[2], &raw_values[2]);
		pc3x3_otp_raw_read_word(otp, raw_addresses[3], &raw_values[3]);
		result = raw_values[0] | raw_values[1] |
			 raw_values[2] | raw_values[3];
		break;

	default:
		err = -EINVAL;
	}

	if (!err)
		*word = result;

	return err;
}

/*
 * Find out how big the region is. We have a 16KB device which can be split
 * equally into 1, 2, 4 or 8 regions. If a partition is redundant or
 * differential redundancy then this is 2 bits of storage per data bit so half
 * the size. For differential-redundant redundancy, 1 bit of data takes 4 bits
 * of storage so divide by 4.
 */
static ssize_t pc3x3_otp_region_get_size(struct otp_region *region)
{
	struct pc3x3_otp *otp = dev_get_drvdata(region->dev.parent);
	int num_regions = pc3x3_otp_num_regions(otp);
	enum otp_redundancy_fmt fmt = __pc3x3_otp_region_get_fmt(otp, region);
	ssize_t region_sz;

	region_sz = (SZ_16K / num_regions);
	if (OTP_REDUNDANCY_FMT_REDUNDANT == fmt ||
	    OTP_REDUNDANCY_FMT_DIFFERENTIAL == fmt)
		region_sz /= 2;
	else if (fmt == OTP_REDUNDANCY_FMT_DIFFERENTIAL_REDUNDANT)
		region_sz /= 4;

	return region_sz;
}

static const struct otp_region_ops pc3x3_region_ops = {
	.set_fmt	= pc3x3_otp_region_set_fmt,
	.get_fmt	= pc3x3_otp_region_get_fmt,
	.get_size	= pc3x3_otp_region_get_size,
};

static int pc3x3_otp_register_regions(struct pc3x3_otp *dev,
				      bool need_unlocked)
{
	struct otp_device *otp = dev->dev;
	int err = 0, i, nr_regions = otp->ops->get_nr_regions(otp);

	for (i = 0; i < nr_regions; ++i) {
		struct otp_region *region;

		if (test_and_set_bit(i, &dev->registered_regions))
			continue;

		region = need_unlocked ?
			otp_region_alloc_unlocked(otp, &pc3x3_region_ops, i,
						  "region%d", i) :
			otp_region_alloc(otp, &pc3x3_region_ops, i,
					 "region%d", i);
		if (IS_ERR(region)) {
			err = PTR_ERR(region);
			break;
		}
	}

	return err;
}

static ssize_t pc3x3_otp_get_nr_regions(struct otp_device *dev)
{
	struct pc3x3_otp *otp = dev_get_drvdata(&dev->dev);
	unsigned long sr = pc3x3_otp_read_sr(otp);
	u32 addr_mask = sr & SR_AXI_ADDRESS_MASK;

	if (0 == addr_mask)
		return 1;
	else if (4 == addr_mask)
		return 2;
	else if (6 == addr_mask)
		return 4;
	else if (7 == addr_mask)
		return 8;

	return -EINVAL;
}

static const struct otp_device_ops pc3x3_otp_ops = {
	.name		= "PC3X3",
	.owner		= THIS_MODULE,
	.get_nr_regions	= pc3x3_otp_get_nr_regions,
	.set_nr_regions	= pc3x3_otp_set_nr_regions,
	.set_fmt	= pc3x3_otp_redundancy_mode_set,
	.write_word	= pc3x3_otp_write_word,
	.read_word	= pc3x3_otp_read_word,
};

static int __devinit pc3x3_otp_probe(struct platform_device *pdev)
{
	int err;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct otp_device *otp;
	struct pc3x3_otp *pc3x3_dev;

	if (!mem) {
		dev_err(&pdev->dev, "no i/o memory\n");
		return -ENXIO;
	}

	if (!devm_request_mem_region(&pdev->dev, mem->start,
				     resource_size(mem), "otp")) {
		dev_err(&pdev->dev, "unable to request i/o memory\n");
		return -EBUSY;
	}

	pc3x3_dev = devm_kzalloc(&pdev->dev, sizeof(*pc3x3_dev), GFP_KERNEL);
	if (!pc3x3_dev)
		return -ENOMEM;

	if (test_mode) {
		u64 *p = devm_kzalloc(&pdev->dev, SZ_16K + SZ_1K, GFP_KERNEL);
		int i;

		if (!p) {
			err = -ENOMEM;
			goto out;
		}

		pc3x3_dev->mem = p;
		pc3x3_dev->iomem = (void __force __iomem *)p;

		for (i = 0; (u8 *)p < (u8 *)pc3x3_dev->mem + SZ_16K + SZ_1K;
		     ++p, ++i)
			*p = (i & 1) ? ~0LLU : 0LLU;
	} else {
		pc3x3_dev->iomem = devm_ioremap(&pdev->dev, mem->start,
						resource_size(mem));
		if (!pc3x3_dev->iomem) {
			err = -ENOMEM;
			goto out;
		}
		pc3x3_dev->mem = (void __force *)pc3x3_dev->iomem;
	}

	pc3x3_dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc3x3_dev->clk)) {
		dev_err(&pdev->dev, "device has no clk\n");
		err = PTR_ERR(pc3x3_dev->clk);
		goto out;
	}
	clk_enable(pc3x3_dev->clk);

	otp = otp_device_alloc(&pdev->dev, &pc3x3_otp_ops, SZ_16K, 8, 8, 0);
	if (IS_ERR(otp)) {
		err = PTR_ERR(otp);
		goto out_clk_disable;
	}
	otp_dev_set_drvdata(otp, pc3x3_dev);

	pc3x3_dev->dev = otp;
	platform_set_drvdata(pdev, pc3x3_dev);

	err = pc3x3_otp_register_regions(pc3x3_dev, false);
	if (err)
		goto out_unregister;

	goto out;

out_unregister:
	otp_device_unregister(otp);
out_clk_disable:
	clk_disable(pc3x3_dev->clk);
	clk_put(pc3x3_dev->clk);
out:
	return err;
}

static int __devexit pc3x3_otp_remove(struct platform_device *pdev)
{
	struct pc3x3_otp *otp = platform_get_drvdata(pdev);

	otp_device_unregister(otp->dev);
	clk_disable(otp->clk);
	clk_put(otp->clk);

	return 0;
}

#ifdef CONFIG_PM
static int pc3x3_otp_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pc3x3_otp *otp = platform_get_drvdata(pdev);

	pc3x3_otp_write_reg(otp, OTP_MACRO_CMD_REG_OFFSET,
			    OTP_COMMAND_POWER_DOWN);
	clk_disable(otp->clk);

	return 0;
}

static int pc3x3_otp_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pc3x3_otp *otp = platform_get_drvdata(pdev);

	clk_enable(otp->clk);
	pc3x3_otp_write_reg(otp, OTP_MACRO_CMD_REG_OFFSET, OTP_COMMAND_IDLE);

	return 0;
}

static const struct dev_pm_ops pc3x3_otp_pm_ops = {
	.suspend	= pc3x3_otp_suspend,
	.resume		= pc3x3_otp_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver pc3x3_otp_driver = {
	.probe		= pc3x3_otp_probe,
	.remove		= __devexit_p(pc3x3_otp_remove),
	.driver		= {
		.name	= "picoxcell-otp-pc3x3",
#ifdef CONFIG_PM
		.pm	= &pc3x3_otp_pm_ops,
#endif /* CONFIG_PM */
	},
};

static int __init pc3x3_otp_init(void)
{
	return platform_driver_register(&pc3x3_otp_driver);
}
module_init(pc3x3_otp_init);

static void __exit pc3x3_otp_exit(void)
{
	platform_driver_unregister(&pc3x3_otp_driver);
}
module_exit(pc3x3_otp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("OTP memory driver for Picochip PC3X3 devices");

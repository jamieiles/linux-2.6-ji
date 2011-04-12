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
#ifndef __PICOXCELL_FUSE_H__
#define __PICOXCELL_FUSE_H__

#include <linux/device.h>
#include <linux/types.h>

/*
 * A logical group of fuses. This could be a single fuse such as one to
 * disable the memif_arm on a picoXcell device or a group of fuses to
 * represent the serial number or a secure key.
 */
struct picoxcell_fuse_range {
	const char		*name;
	int			start;
	int			end;

	/*
	 * Index of the read once per boot, jtag disable and last time program
	 * fuses. If the read once per boot fuse is blown then this range will
	 * only be able to be read once per boot with valid data. Some fuse
	 * ranges will not have a read once per boot fuse so this will be -1.
	 *
	 * The jtag disable fuse prevents the range being read through the
	 * JTAG interface and the last time program prevents the range from
	 * being overwritten.
	 */
	int			read_once;
	int			jtag_disable;
	int			last_time_prog;

	struct device_attribute	attr;
};

/*
 * Define a fuse range with a given name, start and end fuse index.
 */
#define FUSE_RANGE(__name, __start, __end) { \
		.name			= #__name, \
		.start			= __start, \
		.end			= __end, \
		.read_once		= -1, \
		.jtag_disable		= -1, \
		.last_time_prog		= -1, \
	}

/*
 * Define a fuse range with a given name, start and end fuse index. This range
 * also has protection bits for read once per boot, jtag disable and last time
 * program.
 */
#define FUSE_RANGE_PROTECTED(__name, __start, __end, __read_once, \
			     __last_time, __jtag_disable) { \
		.name			= #__name, \
		.start			= __start, \
		.end			= __end, \
		.read_once		= __read_once, \
		.jtag_disable		= __jtag_disable, \
		.last_time_prog		= __last_time, \
	}, \
	FUSE_RANGE(__name ## _last_time_prog, __last_time, __last_time), \
	FUSE_RANGE(__name ## _read_once, __read_once, __read_once), \
	FUSE_RANGE(__name ## _jtag_disable, __jtag_disable, __jtag_disable)

#define FUSE_RANGE_NULL {}

/*
 * The fuse map to be embedded in the picoxcell-fuse platform device as
 * platform data. The .ltp_fuse gives the global last time program fuse index.
 * If this fuse is blown then no writes to any fuse will be allowed.
 */
struct picoxcell_fuse_map {
	int				nr_fuses;
	int				ltp_fuse;

	/*
	 * The VDDQ supply to the fuse block is external to the chip and is
	 * controlled by an enable pin that controls an external transistor.
	 * This switching will take some time to reach the correct voltage and
	 * these times should be described here. To operate within spec, the
	 * VDDQ voltage should only be applied for a maximum of 1 second in
	 * the device's lifetime.
	 */
	unsigned			vddq_rise_usec;
	unsigned			vddq_fall_usec;
	struct picoxcell_fuse_range	ranges[];
};

#endif /* __PICOXCELL_FUSE_H__ */

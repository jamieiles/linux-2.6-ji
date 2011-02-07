/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#ifndef __PICOXCELL_SOC_H__
#define __PICOXCELL_SOC_H__

enum timer_type {
	TIMER_TYPE_RTC,
	TIMER_TYPE_TIMER,
};

struct picoxcell_timer {
	const char			*name;
	enum timer_type			type;
	unsigned long			base;
	int				irq;
};

struct picoxcell_soc {
	const struct picoxcell_timer	*timers;
	int				nr_timers;
};

extern struct picoxcell_soc *picoxcell_get_soc(void);

#endif /* __PICOXCELL_SOC_H__ */

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
	void				(*init)(void);
	void				(*init_clocks)(void);
};

extern const struct picoxcell_soc *picoxcell_get_soc(void);
extern const struct picoxcell_soc pc3x2_soc;
extern const struct picoxcell_soc pc3x3_soc;
extern const struct picoxcell_soc pc30xx_soc;

#ifdef CONFIG_CPU_FREQ
extern int picoxcell_cpufreq_init(unsigned long min_freq_khz,
				  unsigned long max_freq_khz);
#else /* CONFIG_CPU_FREQ */
static inline int picoxcell_cpufreq_init(unsigned long min_freq_khz,
					 unsigned long max_freq_khz)
{
	return 0;
}
#endif /* CONFIG_CPU_FREQ */

#ifdef CONFIG_PM
extern int picoxcell_init_pm(void (*enter_lowpower)(void),
			     void (*exit_lowpower)(void));
#else /* CONFIG_PM */
static inline int picoxcell_init_pm(void (*enter_lowpower)(void),
				    void (*exit_lowpower)(void))
{
	return 0;
}
#endif /* CONFIG_PM */

struct dentry;
extern struct dentry *picoxcell_debugfs;

extern void picoxcell_clk_debugfs_init(void);

#endif /* __PICOXCELL_SOC_H__ */

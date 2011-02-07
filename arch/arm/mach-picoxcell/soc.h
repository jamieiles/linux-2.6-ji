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

enum picoxcell_features {
	PICOXCELL_FEATURE_PM,
	PICOXCELL_FEATURE_CPUFREQ,
	NR_FEAT_BITS
};

struct picoxcell_soc {
	void				(*init)(void);
	void				(*init_clocks)(void);
	const struct picoxcell_timer	*timers;
	int				nr_timers;
	unsigned long			features[BITS_TO_LONGS(NR_FEAT_BITS)];
};

extern struct picoxcell_soc *picoxcell_get_soc(void);
extern struct picoxcell_soc pc3x2_soc;
extern struct picoxcell_soc pc3x3_soc;

static inline int picoxcell_has_feature(enum picoxcell_features feat)
{
	struct picoxcell_soc *soc = picoxcell_get_soc();

	return test_bit(feat, soc->features);
}

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

#endif /* __PICOXCELL_SOC_H__ */

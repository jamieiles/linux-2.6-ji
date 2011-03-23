/*
 * Copyright (c) 2011 picoChip Designs Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#define pr_fmt(fmt) "picoxcell_cpufreq: " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

static struct {
	struct clk	*arm_clk;
	unsigned long	min, max;
} cpufreq;

/*
 * Initialise the new policy. We allow the PLL to go to the minimum speed but
 * limit it to either 700Mhz or the frequency that corresponds to the clkf
 * value in ARM_PLL_M_NUMBER fuses in the fuse block (if nonzero), whichever
 * is smallest.
 *
 * A change of 20% should take ~2uS so we specify the transition latency as
 * 50uS. This should allow jumps from 400MHz->700MHz within this period.
 */
static int picoxcell_cpufreq_init_policy(struct cpufreq_policy *policy)
{
	policy->cpuinfo.min_freq		= cpufreq.min;
	policy->cpuinfo.max_freq		= cpufreq.max;
	policy->cpuinfo.transition_latency	= 50000;
	policy->min				= cpufreq.min;
	policy->max				= cpufreq.max;
	policy->cur = clk_get_rate(cpufreq.arm_clk) / 1000;

	return 0;
}

static int picoxcell_cpufreq_verify(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	policy->min = clk_round_rate(cpufreq.arm_clk, policy->min * 1000) /
		1000;
	policy->max = clk_round_rate(cpufreq.arm_clk, policy->max * 1000) /
		1000;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	return 0;
}

static int picoxcell_cpufreq_target(struct cpufreq_policy *policy,
				    unsigned int target_freq,
				    unsigned int relation)
{
	int ret = 0;
	struct cpufreq_freqs freqs;

	if (target_freq > policy->max)
		target_freq = policy->max;
	if (target_freq < policy->min)
		target_freq = policy->min;

	target_freq = clk_round_rate(cpufreq.arm_clk, target_freq * 1000);

	freqs.old = clk_get_rate(cpufreq.arm_clk) / 1000;
	freqs.new = target_freq / 1000;
	freqs.cpu = policy->cpu;

	if (freqs.new == freqs.old)
		return 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	ret = clk_set_rate(cpufreq.arm_clk, target_freq);
	if (ret) {
		pr_err("unable to set cpufreq rate to %u\n", target_freq);
		freqs.new = freqs.old;
	} else
		freqs.new = clk_get_rate(cpufreq.arm_clk) / 1000;

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

static unsigned int picoxcell_cpufreq_get(unsigned int cpu)
{
	return (unsigned int)clk_get_rate(cpufreq.arm_clk) / 1000;
}

static struct cpufreq_driver picoxcell_cpufreq_driver = {
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_STICKY,
	.name		= "picoxcell",
	.init		= picoxcell_cpufreq_init_policy,
	.verify		= picoxcell_cpufreq_verify,
	.target		= picoxcell_cpufreq_target,
	.get		= picoxcell_cpufreq_get,
};

int picoxcell_cpufreq_init(unsigned long min_freq_khz,
			   unsigned long max_freq_khz)
{
	int err;

	cpufreq.arm_clk = clk_get(NULL, "arm");
	cpufreq.min = min_freq_khz;
	cpufreq.max = max_freq_khz;

	if (IS_ERR(cpufreq.arm_clk)) {
		pr_info("cpufreq: no arm clock available - disabling scaling\n");
		return PTR_ERR(cpufreq.arm_clk);
	}

	err = cpufreq_register_driver(&picoxcell_cpufreq_driver);
	if (!err)
		pr_info("registered cpufreq driver (%luKHz--%luKHz)\n",
			cpufreq.min, cpufreq.max);
	return err;
}

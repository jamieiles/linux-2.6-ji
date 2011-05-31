/*
 * Copyright (c) 2010 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/cnt32_to_63.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>

#include <mach/hardware.h>

#include "picoxcell_core.h"
#include "soc.h"

enum timer_id {
	TIMER_ID_CLOCKEVENT,
	TIMER_ID_CLOCKSOURCE,
	NR_TIMERS,
};

struct timer_instance {
	void __iomem	    *base;
	struct irqaction    irqaction;
};

/*
 * We expect to have 2 timers - a freerunning one for the clock source and a
 * periodic/oneshot one for the clock_event_device.
 */
static struct timer_instance timers[NR_TIMERS];

static void timer_set_mode(enum clock_event_mode mode,
			   struct clock_event_device *clk)
{
	struct timer_instance *timer = &timers[TIMER_ID_CLOCKEVENT];
	unsigned long load_count = DIV_ROUND_UP(CLOCK_TICK_RATE, HZ);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/*
		 * By default, use the kernel tick rate. The reload value can
		 * be changed with the timer_set_next_event() function.
		 */
		__raw_writel(load_count,
			     timer->base + TIMER_LOAD_COUNT_REG_OFFSET);
		__raw_writel(TIMER_ENABLE | TIMER_MODE,
			     timer->base + TIMER_CONTROL_REG_OFFSET);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		__raw_writel(0, timer->base + TIMER_CONTROL_REG_OFFSET);
		break;
	}
}

static int timer_set_next_event(unsigned long evt,
				struct clock_event_device *clk)
{
	struct timer_instance *timer = &timers[TIMER_ID_CLOCKEVENT];

	/* Disable the timer, write the new event then enable it. */
	__raw_writel(0, timer->base + TIMER_CONTROL_REG_OFFSET);
	__raw_writel(evt, timer->base + TIMER_LOAD_COUNT_REG_OFFSET);
	__raw_writel(TIMER_ENABLE | TIMER_MODE,
		     timer->base + TIMER_CONTROL_REG_OFFSET);

	return 0;
}

static struct clock_event_device clockevent_picoxcell = {
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= timer_set_next_event,
	.set_mode		= timer_set_mode,
};

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct timer_instance *timer = &timers[TIMER_ID_CLOCKEVENT];

	/* Clear the interrupt. */
	__raw_readl(timer->base + TIMER_EOI_REG_OFFSET);

	clockevent_picoxcell.event_handler(&clockevent_picoxcell);

	return IRQ_HANDLED;
}

#define PICOXCELL_MIN_RANGE	4

static void picoxcell_clockevent_init(struct picoxcell_soc *soc)
{
	struct timer_instance *inst = &timers[TIMER_ID_CLOCKEVENT];
	const struct picoxcell_timer *timer = NULL;
	int i;

	for (i = 0; i < soc->nr_timers; ++i)
		if (soc->timers[i].type == TIMER_TYPE_TIMER) {
			timer = &soc->timers[i];
			break;
		}

	BUG_ON(!timer);

	/* Configure the interrupt for this timer. */
	inst->irqaction.name	= timer->name;
	inst->irqaction.handler	= timer_interrupt;
	inst->irqaction.flags	= IRQF_DISABLED | IRQF_TIMER;
	inst->base		= ioremap(timer->base, TIMER_SPACING);

	clockevent_picoxcell.name = timer->name;
	clockevents_calc_mult_shift(&clockevent_picoxcell, CLOCK_TICK_RATE,
				    PICOXCELL_MIN_RANGE);
	clockevent_picoxcell.max_delta_ns =
		clockevent_delta2ns(0xfffffffe, &clockevent_picoxcell);
	clockevent_picoxcell.min_delta_ns = 50000;
	clockevent_picoxcell.cpumask = cpumask_of(0);

	/* Start with the timer disabled and the interrupt enabled. */
	__raw_writel(0, inst->base + TIMER_CONTROL_REG_OFFSET);
	setup_irq(timer->irq, &inst->irqaction);

	clockevents_register_device(&clockevent_picoxcell);
}

static cycle_t picoxcell_rtc_get_cycles(struct clocksource *cs)
{
	struct timer_instance *inst = &timers[TIMER_ID_CLOCKSOURCE];

	return __raw_readl(inst->base + RTCLK_CCV_REG_OFFSET);
}

static struct clocksource clocksource_picoxcell = {
	.name	    = "rtc",
	.rating     = 300,
	.read	    = picoxcell_rtc_get_cycles,
	.mask	    = CLOCKSOURCE_MASK(32),
	.flags	    = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void picoxcell_clocksource_init(struct picoxcell_soc *soc)
{
	const struct picoxcell_timer *timer = NULL;
	int i;

	for (i = 0; i < soc->nr_timers; ++i)
		if (soc->timers[i].type == TIMER_TYPE_RTC) {
			timer = &soc->timers[i];
			break;
		}

	BUG_ON(!timer);

	timers[TIMER_ID_CLOCKSOURCE].base = ioremap(timer->base, SZ_4K);

	/* The RTC is always running. We don't need to do any initialization. */
	clocksource_picoxcell.read = picoxcell_rtc_get_cycles;
	clocksource_register_hz(&clocksource_picoxcell, CLOCK_TICK_RATE);
}

static void __init picoxcell_timer_init(void)
{
	struct picoxcell_soc *soc = picoxcell_get_soc();

	picoxcell_clocksource_init(soc);
	picoxcell_clockevent_init(soc);
}

struct sys_timer picoxcell_sys_timer = {
	.init	= picoxcell_timer_init,
};

/*
 * picoxcell's sched_clock implementation. It has a resolution of 5ns
 * (200MHz).
 */
static DEFINE_CLOCK_DATA(cd);

/*
 * Constants generated by:
 *	clocks_calc_mult_shift(m, s, 200000000, NSEC_PER_SEC, 0);
 */
#define SC_MULT		2684354560LU
#define SC_SHIFT	29

unsigned long long notrace sched_clock(void)
{
	u32 cyc = __raw_readl(IO_ADDRESS(PICOXCELL_RTCLK_BASE) +
			      RTCLK_CCV_REG_OFFSET);
	return cyc_to_fixed_sched_clock(&cd, cyc, (u32)~0, SC_MULT, SC_SHIFT);
}

static void notrace picoxcell_update_sched_clock(void)
{
	u32 cyc = __raw_readl(IO_ADDRESS(PICOXCELL_RTCLK_BASE) +
			      RTCLK_CCV_REG_OFFSET);
	update_sched_clock(&cd, cyc, (u32)~0);
}

void __init picoxcell_sched_clock_init(void)
{
	/*
	 * Reset the RTC. We don't know how long the RTC has been running for
	 * in the bootloader.
	 */
	__raw_writel(0, IO_ADDRESS(PICOXCELL_RTCLK_BASE +
				   RTCLK_SET_REG_OFFSET));
	init_fixed_sched_clock(&cd, picoxcell_update_sched_clock, 32,
			       CLOCK_TICK_RATE, SC_MULT, SC_SHIFT);
}

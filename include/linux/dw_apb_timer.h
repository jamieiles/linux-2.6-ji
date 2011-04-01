/*
 * (C) Copyright 2009 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * Shared with ARM platforms, Jamie Iles, Picochip 2011
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Support for the Synopsys DesignWare APB Timers.
 */
#ifndef __DW_APB_TIMER_H__
#define __DW_APB_TIMER_H__

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>

#define APBTMRS_REG_SIZE       0x14

struct dw_apb_timer {
	void __iomem				*base;
	unsigned long				freq;
	int					irq;
};

struct dw_apb_clock_event_device {
	struct clock_event_device		ced;
	struct dw_apb_timer			timer;
	struct irqaction			irqaction;
	void					(*eoi)(struct dw_apb_timer *);
};

struct dw_apb_clocksource {
	struct clocksource			cs;
	struct dw_apb_timer			timer;
};

/**
 * dw_apb_clockevent_register() - register the clock with the generic layer
 *
 * @dw_ced:	The APB clock to register as a clock_event_device.
 */
void dw_apb_clockevent_register(struct dw_apb_clock_event_device *dw_ced);

/**
 * dw_apb_clockevent_pause() - stop the clock_event_device from running
 *
 * @dw_ced:	The APB clock to stop generating events.
 */
void dw_apb_clockevent_pause(struct dw_apb_clock_event_device *dw_ced);

/**
 * dw_apb_clockevent_resume() - resume a clock that has been paused.
 *
 * @dw_ced:	The APB clock to resume.
 */
void dw_apb_clockevent_resume(struct dw_apb_clock_event_device *dw_ced);

/**
 * dw_apb_clockevent_stop() - stop the clock_event_device and release the IRQ.
 *
 * @dw_ced:	The APB clock to stop generating the events.
 */
void dw_apb_clockevent_stop(struct dw_apb_clock_event_device *dw_ced);

/**
 * dw_apb_clockevent_init() - use an APB timer as a clock_event_device
 *
 * @cpu:	The CPU the events will be targeted at.
 * @name:	The name used for the timer and the IRQ for it.
 * @rating:	The rating to give the timer.
 * @base:	I/O base for the timer registers.
 * @irq:	The interrupt number to use for the timer.
 * @freq:	The frequency that the timer counts at.
 *
 * This creates a clock_event_device for using with the generic clock layer
 * but does not start and register it.  This should be done with
 * dw_apb_clockevent_register() as the next step.  If this is the first time
 * it has been called for a timer then the IRQ will be requested, if not it
 * just be enabled to allow CPU hotplug to avoid repeatedly requesting and
 * releasing the IRQ.
 */
struct dw_apb_clock_event_device *
dw_apb_clockevent_init(int cpu, const char *name, unsigned rating,
		       void __iomem *base, int irq, unsigned long freq);

/**
 * dw_apb_clocksource_init() - use an APB timer as a clocksource.
 *
 * @rating:	The rating to give the clocksource.
 * @name:	The name for the clocksource.
 * @base:	The I/O base for the timer registers.
 * @freq:	The frequency that the timer counts at.
 *
 * This creates a clocksource using an APB timer but does not yet register it
 * with the clocksource system.  This should be done with
 * dw_apb_clocksource_register() as the next step.
 */
struct dw_apb_clocksource *
dw_apb_clocksource_init(unsigned rating, char *name, void __iomem *base,
			unsigned long freq);

/**
 * dw_apb_clocksource_register() - register the APB clocksource.
 *
 * @dw_cs:	The clocksource to register.
 */
void dw_apb_clocksource_register(struct dw_apb_clocksource *dw_cs);

/**
 * dw_apb_clocksource_start() - start the clocksource counting.
 *
 * @dw_cs:	The clocksource to start.
 *
 * This is used to start the clocksource before registration and can be used
 * to enable calibration of timers.
 */
void dw_apb_clocksource_start(struct dw_apb_clocksource *dw_cs);

/**
 * dw_apb_clocksource_read() - read the current value of a clocksource.
 *
 * @dw_cs:	The clocksource to read.
 */
cycle_t dw_apb_clocksource_read(struct dw_apb_clocksource *dw_cs);

#endif /* __DW_APB_TIMER_H__ */

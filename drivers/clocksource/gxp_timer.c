// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sched_clock.h>
#include <linux/cpumask.h>
#include <linux/cpuhotplug.h>
#include <linux/clockchips.h>

#include <asm/irq.h>

#define TIMER0_FREQ 1000000
#define TIMER1_FREQ 1000000

#define MASK_TCS_ENABLE		0x01
#define MASK_TCS_PERIOD		0x02
#define MASK_TCS_RELOAD		0x04
#define MASK_TCS_TC		0x80

struct gxp_timer {
	void __iomem *counter;
	void __iomem *control;
	struct clock_event_device evt;
};

struct clock_event_device evt2;					// For core 1
static cpumask_var_t cpu1_mask __cpumask_var_read_mostly;
static void __iomem *system_clock __read_mostly;

static u64 notrace gxp_sched_read(void)
{
	return readl_relaxed(system_clock);
}

static int gxp_time_set_next_event(unsigned long event,
					struct clock_event_device *evt_dev)
{
	struct gxp_timer *timer = container_of(evt_dev, struct gxp_timer, evt);
	// clear TC by write 1 and disable timer int and counting
	writeb_relaxed(MASK_TCS_TC, timer->control);
	// update counter value
	writel_relaxed(event, timer->counter);
	// enable timer counting and int
	writeb_relaxed(MASK_TCS_TC|MASK_TCS_ENABLE, timer->control);

	return 0;
}

static int gxp_time_set_next_event_dummy(unsigned long event,
					struct clock_event_device *evt_dev)
{
	return 0;
}

static irqreturn_t gxp_time_interrupt(int irq, void *dev_id)
{
	struct gxp_timer *timer = dev_id;
	void (*event_handler)(struct clock_event_device *timer);
	
#ifdef CONFIG_ARCH_HPE_GSC
	// TODO: Remove for B0 ASIC.
	if(cpumask_test_cpu(1, cpu_online_mask)) {
		tick_broadcast(cpu1_mask);
	}
#endif // CONFIG_ARCH_HPE_GSC

	if (readb_relaxed(timer->control) & MASK_TCS_TC) {
		writeb_relaxed(MASK_TCS_TC, timer->control);

		event_handler = READ_ONCE(timer->evt.event_handler);
		if (event_handler)
			event_handler(&timer->evt);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int gxp_timer_starting_cpu(unsigned int cpu)
{
	pr_info("CPU[%d]: %s cpu=%d\n", smp_processor_id(), __FUNCTION__, cpu);

	// We would register per cpu clock event device for cpu 0 in driver entry function.
	if(cpu == 0)
		return 0;

	evt2.name = "gxp_timer2";
	evt2.rating = 300;
	evt2.features = CLOCK_EVT_FEAT_ONESHOT;
	evt2.set_next_event = gxp_time_set_next_event_dummy;
	evt2.cpumask = cpumask_of(cpu);

	clockevents_config_and_register(&evt2, TIMER0_FREQ,
					0xf, 0xffffffff);

	return 0;
}

static int gxp_timer_dying_cpu(unsigned int cpu)
{
	pr_info("CPU[%d]: %s cpu=%d\n", smp_processor_id(), __FUNCTION__, cpu);
	return 0;
}

static int __init gxp_timer_init(struct device_node *node)
{
	void __iomem *base_counter;
	void __iomem *base_control;
	u32 freq;
	int ret, irq;
	struct gxp_timer *gxp_timer;

	base_counter = of_iomap(node, 0);
	if (!base_counter) {
		pr_err("Can't remap counter registers");
		return -ENXIO;
	}

	base_control = of_iomap(node, 1);
	if (!base_control) {
		pr_err("Can't remap control registers");
		return -ENXIO;
	}

	system_clock = of_iomap(node, 2);
	if (!system_clock) {
		pr_err("Can't remap control registers");
		return -ENXIO;
	}

	if (of_property_read_u32(node, "clock-frequency", &freq)) {
		pr_err("Can't read clock-frequency\n");
		goto err_iounmap;
	}

	sched_clock_register(gxp_sched_read, 32, freq);
	clocksource_mmio_init(system_clock, node->name, freq,
				300, 32, clocksource_mmio_readl_up);

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		ret = -EINVAL;
		pr_err("GXP Timer Can't parse IRQ %d", irq);
		goto err_iounmap;
	}

	gxp_timer = kzalloc(sizeof(*gxp_timer), GFP_KERNEL);
	if (!gxp_timer) {
		ret = -ENOMEM;
		goto err_iounmap;
	}

	cpumask_set_cpu(1, cpu1_mask);

	gxp_timer->counter = base_counter;
	gxp_timer->control = base_control;
	gxp_timer->evt.name = node->name;
	gxp_timer->evt.rating = 300;
	gxp_timer->evt.features = CLOCK_EVT_FEAT_ONESHOT;
	gxp_timer->evt.set_next_event = gxp_time_set_next_event;
	gxp_timer->evt.cpumask = cpumask_of(0);

	if (request_irq(irq, gxp_time_interrupt, IRQF_TIMER | IRQF_SHARED,
		node->name, gxp_timer)) {
		pr_err("%s: request_irq() failed\n", "GXP Timer Tick");
		goto err_timer_free;
	}

#ifdef CONFIG_ARCH_HPE_GSC
	// TODO: Remove for B0 ASIC.
	/* Register and immediately configure the timer on the boot CPU */
	ret = cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_STARTING,
				"gxp_timer per cpu callback: starting",
				gxp_timer_starting_cpu, gxp_timer_dying_cpu);
#endif // CONFIG_ARCH_HPE_GSC

	clockevents_config_and_register(&gxp_timer->evt, TIMER0_FREQ,
					0xf, 0xffffffff);

	pr_info("gxp: system timer (irq = %d)\n", irq);
	return 0;


err_timer_free:
	kfree(gxp_timer);

err_iounmap:
	iounmap(system_clock);
	iounmap(base_control);
	iounmap(base_counter);
	return ret;
}

TIMER_OF_DECLARE(gxp, "hpe,gxp-timer", gxp_timer_init);

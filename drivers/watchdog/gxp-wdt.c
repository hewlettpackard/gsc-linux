// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#if CONFIG_ARM_ARCH_TIMER
#include <linux/of_address.h>
#include <linux/of_platform.h>
#endif

#define MASK_WDGCS_ENABLE	0x01
#define MASK_WDGCS_RELOAD	0x04
#define MASK_WDGCS_NMIEN	0x08
#define MASK_WDGCS_WARN		0x80

#define WDT_MAX_TIMEOUT_MS	655350
#define WDT_DEFAULT_TIMEOUT	30
#define SECS_TO_WDOG_TICKS(x) ((x) * 100)
#define WDOG_TICKS_TO_SECS(x) ((x) / 100)

#define GXP_WDT_CNT_OFS		0x10
#define GXP_WDT_CTRL_OFS	0x16

struct gxp_wdt {
#if CONFIG_ARM_ARCH_TIMER
	void __iomem *counter;
	void __iomem *control;
#else
	void __iomem *base;
#endif
	struct watchdog_device wdd;
};

static void gxp_wdt_enable_reload(struct gxp_wdt *drvdata)
{
	u8 val;

#if CONFIG_ARM_ARCH_TIMER
	val = readb(drvdata->control);
	val |= (MASK_WDGCS_ENABLE | MASK_WDGCS_RELOAD);
	writeb(val, drvdata->control);
#else
	val = readb(drvdata->base + GXP_WDT_CTRL_OFS);
	val |= (MASK_WDGCS_ENABLE | MASK_WDGCS_RELOAD);
	writeb(val, drvdata->base + GXP_WDT_CTRL_OFS);
#endif
}

static int gxp_wdt_start(struct watchdog_device *wdd)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);

#if CONFIG_ARM_ARCH_TIMER
	writew(SECS_TO_WDOG_TICKS(wdd->timeout), drvdata->counter);
#else
	writew(SECS_TO_WDOG_TICKS(wdd->timeout), drvdata->base + GXP_WDT_CNT_OFS);
#endif
	gxp_wdt_enable_reload(drvdata);
	return 0;
}

static int gxp_wdt_stop(struct watchdog_device *wdd)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);
	u8 val;

#if CONFIG_ARM_ARCH_TIMER
	val = readb_relaxed(drvdata->control);
	val &= ~MASK_WDGCS_ENABLE;
	writeb(val, drvdata->control);
#else
	val = readb_relaxed(drvdata->base + GXP_WDT_CTRL_OFS);
	val &= ~MASK_WDGCS_ENABLE;
	writeb(val, drvdata->base + GXP_WDT_CTRL_OFS);
#endif
	return 0;
}

static int gxp_wdt_set_timeout(struct watchdog_device *wdd,
			       unsigned int timeout)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);
	u32 actual;

	wdd->timeout = timeout;
#if CONFIG_ARM_ARCH_TIMER
	actual = min(timeout, wdd->max_hw_heartbeat_ms / 1000);
	writew((SECS_TO_WDOG_TICKS(actual)), drvdata->counter);
#else
	actual = min(timeout * 100, wdd->max_hw_heartbeat_ms / 10);
	writew(actual, drvdata->base + GXP_WDT_CNT_OFS);
#endif

	return 0;
}

static unsigned int gxp_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);
#if CONFIG_ARM_ARCH_TIMER
	u32 val = readw(drvdata->counter);
#else
	u32 val = readw(drvdata->base + GXP_WDT_CNT_OFS);
#endif

	return WDOG_TICKS_TO_SECS(val);
}

static int gxp_wdt_ping(struct watchdog_device *wdd)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);

	gxp_wdt_enable_reload(drvdata);
	return 0;
}

static int gxp_restart(struct watchdog_device *wdd, unsigned long action,
		       void *data)
{
	struct gxp_wdt *drvdata = watchdog_get_drvdata(wdd);

#if CONFIG_ARM_ARCH_TIMER
	writew(10, drvdata->counter);
#else
	writew(1, drvdata->base + GXP_WDT_CNT_OFS);
#endif
	gxp_wdt_enable_reload(drvdata);
	mdelay(100);
	return 0;
}

static const struct watchdog_ops gxp_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	gxp_wdt_start,
	.stop =		gxp_wdt_stop,
	.ping =		gxp_wdt_ping,
	.set_timeout =	gxp_wdt_set_timeout,
	.get_timeleft =	gxp_wdt_get_timeleft,
	.restart =	gxp_restart,
};

static const struct watchdog_info gxp_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "HPE GXP Watchdog timer",
};

static int gxp_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gxp_wdt *drvdata;
	int err;
	u8 val;

	drvdata = devm_kzalloc(dev, sizeof(struct gxp_wdt), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	/*
	 * The register area where the timer and watchdog reside is disarranged.
	 * Hence mapping individual register blocks for the timer and watchdog
	 * is not recommended as they would have access to each others
	 * registers. Based on feedback the watchdog is no longer part of the
	 * device tree file and the timer driver now creates the watchdog as a
	 * child device. During the watchdogs creation, the timer driver passes
	 * the base address to the watchdog over the private interface.
	 */

#if CONFIG_ARM_ARCH_TIMER
	struct resource *res;
	platform_set_drvdata(pdev, drvdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->counter = devm_ioremap_resource(dev, res);
	if (IS_ERR(drvdata->counter))
		return PTR_ERR(drvdata->counter);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	drvdata->control = devm_ioremap_resource(dev, res);
	if (IS_ERR(drvdata->control))
		return PTR_ERR(drvdata->control);
#else
	drvdata->base = (void __iomem *)dev->platform_data;
#endif

	drvdata->wdd.info = &gxp_wdt_info;
	drvdata->wdd.ops = &gxp_wdt_ops;
	drvdata->wdd.max_hw_heartbeat_ms = WDT_MAX_TIMEOUT_MS;
	drvdata->wdd.parent = dev;
	drvdata->wdd.timeout = WDT_DEFAULT_TIMEOUT;

	watchdog_set_drvdata(&drvdata->wdd, drvdata);
	watchdog_set_nowayout(&drvdata->wdd, WATCHDOG_NOWAYOUT);

#if CONFIG_ARM_ARCH_TIMER
	val = readb(drvdata->control);
#else
	val = readb(drvdata->base + GXP_WDT_CTRL_OFS);
#endif

	if (val & MASK_WDGCS_ENABLE)
		set_bit(WDOG_HW_RUNNING, &drvdata->wdd.status);

	watchdog_set_restart_priority(&drvdata->wdd, 128);

	watchdog_stop_on_reboot(&drvdata->wdd);
	err = devm_watchdog_register_device(dev, &drvdata->wdd);
	if (err) {
		dev_err(dev, "Failed to register watchdog device");
		return err;
	}

	dev_info(dev, "HPE GXP watchdog timer");

	return 0;
}

#if CONFIG_ARM_ARCH_TIMER
static void gxp_wdt_remove(struct platform_device *pdev)
{
		return;
}

static const struct of_device_id gxp_wdt_of_match[] = {
	{ .compatible = "hpe,gxp-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_wdt_of_match);
#endif

static struct platform_driver gxp_wdt_driver = {
	.probe = gxp_wdt_probe,
#if CONFIG_ARM_ARCH_TIMER
	.remove	= gxp_wdt_remove,
	.driver = {
		.name =	"gxp-wdt",
		.of_match_table = gxp_wdt_of_match,
	},
#else
	.driver = {
		.name =	"gxp-wdt",
	},
#endif
};
module_platform_driver(gxp_wdt_driver);

MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("Driver for GXP watchdog timer");
MODULE_LICENSE("GPL");

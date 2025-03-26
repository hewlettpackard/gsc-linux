// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2025 Hewlett Packard Enterprise Development LP */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sysfs.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "linux/gxp-soclib.h"

#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1

DECLARE_WAIT_QUEUE_HEAD(gxp_cpu);

enum xreg_gpio_pn {
	CPU1_PRST,
	CPU2_PRST,
	CPU3_PRST,
	CPU4_PRST
};

struct gxp_cpu_drvdata {
	void __iomem *base;
	struct gpio_chip gpio_chip;
	struct regmap *cpu_map;
	int irq;
};

static int gxp_cpu_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_cpu_drvdata *drvdata = dev_get_drvdata(chip->parent);
	unsigned int val;
	int ret = 0;
	// reset scan chain
	regmap_write(drvdata->cpu_map, 0, 0);
	regmap_read(drvdata->cpu_map, 0, &val);
	regmap_read(drvdata->cpu_map, 0, &val);
	regmap_read(drvdata->cpu_map, 0, &val);
	regmap_read(drvdata->cpu_map, 0, &val);
	//  That is a scan chain so we must read address 0x4 by issuing 5 read command
	regmap_read(drvdata->cpu_map, 0, &val);

	switch (offset) {
	case CPU1_PRST:
		ret = (val & BIT(0)) ? 1 : 0;
		break;
	case CPU2_PRST:
		ret = (val & BIT(1)) ? 1 : 0;
		break;
	case CPU3_PRST:
		ret = (val & BIT(2)) ? 1 : 0;
		break;
	case CPU4_PRST:
		ret = (val & BIT(3)) ? 1 : 0;
		break;
	default:
		break;
	}

	return ret;
}

static void gxp_cpu_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
}

static int gxp_cpu_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	int ret = GPIO_DIR_IN;

	return ret;
}

static int gxp_cpu_gpio_direction_input(struct gpio_chip *chip,
					unsigned int offset)
{
	int ret = -EOPNOTSUPP;

	switch (offset) {
	case CPU1_PRST:
	case CPU2_PRST:
	case CPU3_PRST:
	case CPU4_PRST:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_cpu_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int value)
{
	int ret = -EOPNOTSUPP;

	return ret;
}

static void gxp_cpu_gpio_irq_ack(struct irq_data *d)
{
}

static void gxp_cpu_gpio_irq_set_mask(struct irq_data *d, bool set)
{
}

static void gxp_cpu_gpio_irq_mask(struct irq_data *d)
{
}

static void gxp_cpu_gpio_irq_unmask(struct irq_data *d)
{
}

static int gxp_cpu_gpio_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static irqreturn_t gxp_cpu_irq_handle(int irq, void *_drvdata)
{
	struct gxp_cpu_drvdata *drvdata = (struct gxp_cpu_drvdata *)_drvdata;
	unsigned int val, girq;

	return IRQ_HANDLED;
}

static const struct gpio_chip cpu_chip = {
	.label			= "gxp-cpu",
	.owner			= THIS_MODULE,
	.get			= gxp_cpu_gpio_get,
	.set			= gxp_cpu_gpio_set,
	.get_direction = gxp_cpu_gpio_get_direction,
	.direction_input = gxp_cpu_gpio_direction_input,
	.direction_output = gxp_cpu_gpio_direction_output,
	.base = -1,
	//.can_sleep		= true,
};

static struct irq_chip gxp_gpio_irqchip = {
	.name		= "gxp-cpu",
	.irq_ack	= gxp_cpu_gpio_irq_ack,
	.irq_mask	= gxp_cpu_gpio_irq_mask,
	.irq_unmask	= gxp_cpu_gpio_irq_unmask,
	.irq_set_type	= gxp_cpu_gpio_set_type,
};

static const struct of_device_id gxp_cpu_of_match[] = {
	{ .compatible = "hpe,gxp-cpu" },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_cpu_of_match);

static int gxp_cpu_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_cpu_drvdata *drvdata;
	struct resource *res;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_cpu_drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	dev_info(&pdev->dev, "gxp-cpu: Loading gxp-cpu driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->cpu_map = syscon_regmap_lookup_by_compatible("hpe,gxp-cpu");
	if (IS_ERR(drvdata->cpu_map)) {
		dev_err(&pdev->dev, "Unable to find fn2 regmap\n");
		return PTR_ERR(drvdata->cpu_map);
	}

	drvdata->gpio_chip = cpu_chip;
	drvdata->gpio_chip.ngpio = 16;
	drvdata->gpio_chip.parent = &pdev->dev;
	dev_info(&pdev->dev, "Max GPIOS %d\n", ARCH_NR_GPIOS);

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "Could not register gpiochip for cpu presence, %d\n", ret);

#if 0 //Jean Marie knows why this is commented out
	ret = gpiochip_irqchip_add(&drvdata->gpio_chip,
				   &gxp_gpio_irqchip, 0, handle_edge_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_info(&pdev->dev, "Could not add irqchip - %d\n", ret);
		gpiochip_remove(&drvdata->gpio_chip);
		return ret;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Get irq from platform fail - %d\n", ret);
		return ret;
	}
	drvdata->irq = ret;

	ret = devm_request_irq(&pdev->dev, drvdata->irq, gxp_cpu_irq_handle,
			       IRQF_SHARED, "gxp-cpu", drvdata);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ handler failed - %d\n", ret);
		return ret;
	}
#endif

	return 0;
}

static struct platform_driver gxp_cpu_driver = {
	.probe = gxp_cpu_probe,
	.driver = {
		.name = "gxp-cpu",
		.of_match_table = of_match_ptr(gxp_cpu_of_match),
	},
};
module_platform_driver(gxp_cpu_driver);

MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("HPE GXP CPU Presence Driver");

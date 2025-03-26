// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/syscon.h>
#include <linux/interrupt.h>

#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1


struct gxp_gpio_drvdata {
	struct regmap *vuhc0_map;
	struct gpio_chip chip;
	int irq;
};

static int misc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_gpio_drvdata *drvdata = dev_get_drvdata(chip->parent);
	unsigned int val;
	int ret = 0;

	switch (offset) {
	case 0 ... 7:
		//offset 0x64-0x80 bit 13
		regmap_read(drvdata->vuhc0_map, 0x64 + (offset*4),	&val);
		ret = (val&BIT(13))?1:0;
		break;
	default:
		break;
	}

	return ret;
}

static void misc_gpio_set(struct gpio_chip *chip, unsigned int offset,
		int value)
{
	switch (offset) {
	default:
		break;
	}
}

static int misc_gpio_get_direction(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = 0;

	switch (offset) {
	case 0 ... 7:
		ret = GPIO_DIR_IN;
		break;
	default:
		break;
	}

	return ret;
}

static int misc_gpio_direction_input(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case 0 ... 7:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int misc_gpio_direction_output(struct gpio_chip *chip,
		unsigned int offset, int value)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	default:
		break;
	}

	return ret;
}

const static struct gpio_chip gpio_misc_chip = {
	.label			= "gxp_vuhc",
	.owner			= THIS_MODULE,
	.get			= misc_gpio_get,
	.set			= misc_gpio_set,
	.get_direction = misc_gpio_get_direction,
	.direction_input = misc_gpio_direction_input,
	.direction_output = misc_gpio_direction_output,
	.base = -1,
	//.can_sleep		= true,
};

static int gxp_gpio_misc_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_gpio_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct device *parent;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent\n");
		return -ENODEV;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_gpio_drvdata),
				GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	drvdata->vuhc0_map = syscon_regmap_lookup_by_phandle(dev->of_node,
							"vuhc0_handle");
	if (IS_ERR(drvdata->vuhc0_map)) {
		dev_err(dev, "failed to map vuhc0_handle\n");
		return -ENODEV;
	}

	drvdata->chip = gpio_misc_chip;
	drvdata->chip.ngpio = 50; // 0~49: vuhc misc.
	drvdata->chip.parent = &pdev->dev;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->chip, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "could not register gpiochip, %d\n", ret);

	return 0;
}

static const struct of_device_id gxp_gpio_misc_of_match[] = {
	{ .compatible = "hpe,gxp-gpio-misc"},
	{}
};
MODULE_DEVICE_TABLE(of, gxp_gpio_misc_of_match);

static struct platform_driver gxp_gpio_misc_driver = {
	.driver = {
		.name	= "gxp-gpio-misc",
		.of_match_table = gxp_gpio_misc_of_match,
	},
	.probe		= gxp_gpio_misc_probe,
};
module_platform_driver(gxp_gpio_misc_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("Misc GPIO interface for GXP");

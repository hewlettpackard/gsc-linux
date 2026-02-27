// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2025 Hewlett Packard Enterprise Development LP
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
#include <linux/mfd/syscon.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio/driver.h>
#include <linux/of.h>

#include "gxp-mid.h"


struct gxp_mid_drvdata {
	struct regmap *mid_map;
	struct gpio_chip chip;
	int irq;
};

static int gxp_mid_get(struct gpio_chip *chip, unsigned int offset)
{
	int mid_byte = 0;
	int mid_bit = 0;
	int memid = 0;
	int ret = 0;
	struct gxp_mid_drvdata *drvdata = dev_get_drvdata(chip->parent);

	switch (offset) {
	case 0 ... MID_MAX_BIT:
		mid_byte = (offset / 32) * 4;
		mid_bit  = offset % 32;
		regmap_read(drvdata->mid_map, mid_byte, &memid);
		ret = (memid & BIT(mid_bit)) ? 1 : 0;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	return ret;
}

static void gxp_mid_set(struct gpio_chip *chip, unsigned int offset,
		int value)
{
	switch (offset) {
	case 0 ... MID_MAX_BIT:
		//
		// Scan Chain Bytes are read -only,
		// So, we don't implement writes.
		//
		break;
	default:
		break;
	}
}

static int gxp_mid_get_direction(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = 0;

	switch (offset) {
	case 0 ... MID_MAX_BIT:
		//
		// All Scan Chain Bytes are read-only
		//
		ret = MID_DIR_IN;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_mid_direction_input(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case 0 ... MID_MAX_BIT:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_mid_direction_output(struct gpio_chip *chip,
		unsigned int offset, int value)
{
	int ret = -ENOTSUPP;
	return ret;
}

static const struct gpio_chip mid_chip = {
	.label            = "gxp_mid",
	.owner            = THIS_MODULE,
	.get              = gxp_mid_get,
	.set              = gxp_mid_set,
	.get_direction    = gxp_mid_get_direction,
	.direction_input  = gxp_mid_direction_input,
	.direction_output = gxp_mid_direction_output,
	.base             = -1,
	//.can_sleep        = true,
};

static int gxp_mid_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_mid_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct device *parent;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent\n");
		return -ENODEV;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_mid_drvdata),
				GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	drvdata->mid_map = syscon_regmap_lookup_by_phandle(dev->of_node,
							"mid_handle");
	if (IS_ERR(drvdata->mid_map)) {
		dev_err(dev, "failed to map mid_handle\n");
		return -ENODEV;
	}

	drvdata->chip = mid_chip;
	drvdata->chip.ngpio = MID_MAX_BIT + 1;
	drvdata->chip.parent = &pdev->dev;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->chip, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "could not register gpiochip, %d\n", ret);

	return 0;
}

static const struct of_device_id gxp_mid_of_match[] = {
	{ .compatible = "hpe,gxp-mid"},
	{}
};

MODULE_DEVICE_TABLE(of, gxp_mid_of_match);

static struct platform_driver gxp_mid_driver = {
	.driver = {
		.name   = "gxp-mid",
		.of_match_table = gxp_mid_of_match,
	},
	.probe      = gxp_mid_probe,
};
module_platform_driver(gxp_mid_driver);

MODULE_AUTHOR("Sandesh Kanavalli <sandesh-nagappa.kanavalli@hpe.com>");
MODULE_DESCRIPTION("MEMID ScanCahin interface support for GXP");

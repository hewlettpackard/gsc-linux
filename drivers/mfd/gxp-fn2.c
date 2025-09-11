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
#include <linux/gpio/driver.h>
#include <linux/of.h>

#include "linux/gxp-soclib.h"

#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1

#define FN2_VPBTN	0x46
#define FN2_SEVSTAT	0x70
#define PGOOD_MASK	0x01
#define PERST_MASK	0x02
#define FN2_SEVMASK	0x74

enum xreg_gpio_pn {
	VPBTN = 0,	//out
	PGOOD,		//in
	PERST,		//in
	POST_COMPLETE,	//in
};

struct gxp_fn2_drvdata {
	void __iomem *base;
	struct regmap *fn2_map;
	struct regmap *xreg_map;
	struct gpio_chip gpio_chip;
	unsigned int host_boot_en_called;
	int irq;
};

unsigned int gxp_pgood_trigger;
EXPORT_SYMBOL(gxp_pgood_trigger);

extern short int xreg_server_id;
DECLARE_WAIT_QUEUE_HEAD(gxp_fn2);

static int gxp_fn2_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(chip->parent);
	unsigned int val;
	int ret = 0;

	switch (offset) {
	case PGOOD:
		//offset 0x70 bit 24
		regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);
		ret = (val&BIT(24))?1:0;
		if (ret) {
			// We signal the transition to power good
			pr_info("FN2 PGOOD ENABLED");
			gxp_pgood_trigger = 1;
			wake_up_interruptible(&gxp_fn2);
		} else {
			// We signal the transition to power down
			pr_info("FN2 PGOOD DISABLED");

			regmap_read(drvdata->xreg_map, 0x4, &val);
			if (val & 0xff000000)
				pr_info("FN2 off reg 0x7 = 0x%x\n", (val & 0xff000000) >> 24);

			regmap_read(drvdata->xreg_map, 0x8, &val);
			if (val & 0x000000ff)
				pr_info("FN2 off reg 0x8 = 0x%x\n", val & 0x000000ff);

			gxp_pgood_trigger = 2;
			wake_up_interruptible(&gxp_fn2);
		}
		break;
	case PERST:
		//offset 0x70 bit 25
		regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);
		ret = (val&BIT(25))?1:0;
		break;
	case POST_COMPLETE:
		//todo: read from sram
	default:
		break;
	}

	return ret;
}

static void gxp_fn2_gpio_set(struct gpio_chip *chip, unsigned int offset,
		int value)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(chip->parent);
	unsigned int tmp;

	switch (offset) {
	case VPBTN:
#ifdef CONFIG_ARCH_HPE_GXP
#ifdef CONFIG_HPE_GXP_FIX_GEN11_ME_ISSUE
		if (serverID != 0x250) {
			//
			//Write offset 0x9 with a value of 0x24
			//to indicate that FW validation is complete And
			//the CPLD power sequencer can transition to S5.
			//
			regmap_update_bits(drvdata->xreg_map, 0x08,
					0x0000FF00, 0x00002400);
			//
			// Due to ME bugs on Intel platform we need to reset eSPI OOB ctrl register
			//
			void __iomem *eSPIreg = ioremap(0x80FE1040, 4);

			writel(0x00060001, eSPIreg);
			iounmap(eSPIreg);
		}
#else
		//
		//Write offset 0xF with a value of 0x04
		//to indicate that FW validation is complete
		//
		regmap_update_bits(drvdata->xreg_map, 0x0c,
				0xFF000000, 0x04000000);
#endif
#endif

#ifdef CONFIG_ARCH_HPE_GSC


		regmap_read(drvdata->xreg_map, 0x8, &tmp);
		if (tmp & 0x000000ff) {
			pr_info("FN2 Power Button reg 0x8 = 0x%x\n", tmp & 0x000000ff);
			regmap_update_bits(drvdata->xreg_map, 0x8, 0xff, 0x40 | 0x10 | 0x8 | 0x2 | 0x1);
		}

		//set FW validation bit as Passed and G2_TO_S5 Complete bit (offset 0x09)
		regmap_update_bits(drvdata->xreg_map, 0x08,
				   0x00002400, 0x00002400);
#endif
		//offset 0x44 bit 16
		regmap_update_bits(drvdata->fn2_map, 0x44, BIT(16),
				value == 0?0:BIT(16));
		break;
	default:
		break;
	}
}

static int gxp_fn2_gpio_get_direction(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = GPIO_DIR_IN;

	switch (offset) {
	case VPBTN:
		ret = GPIO_DIR_OUT;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_fn2_gpio_direction_input(struct gpio_chip *chip,
		unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case PGOOD:
	case PERST:
	case POST_COMPLETE:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_fn2_gpio_direction_output(struct gpio_chip *chip,
		unsigned int offset, int value)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case VPBTN:
		gxp_fn2_gpio_set(chip, offset, value);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static ssize_t fn2_host_boot_en_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(dev);
	int ret;

	ret = sprintf(buf, "0x%02x", drvdata->host_boot_en_called);

	return ret;
}

static ssize_t fn2_host_boot_en_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;

	//Dont care about input

	drvdata->host_boot_en_called = 1;

	//Pull the SOC Out of Reset (offset 0x11A)
	regmap_update_bits(drvdata->xreg_map, 0x118,
			   0x00FF0000, 0x00000000);

	//set FW validation bit as Passed and G2_TO_S5 Complete bit (offset 0x09)
	regmap_update_bits(drvdata->xreg_map, 0x08,
			   0x00002400, 0x00002400);

	//offset 0x70 bit 24
	regmap_read(drvdata->fn2_map, 0x40, &value);
	value |= 0x200 | 0x400;
	regmap_write(drvdata->fn2_map, 0x40, value);

	//set PS_ENABLE bit
	regmap_update_bits(drvdata->xreg_map,
			   0x40, 0x0000FF00, 0x0000FF00);

	//set PWR_ON_MASK
	regmap_update_bits(drvdata->xreg_map, 0x48, 0xFF000000, 0x08000000);

	return count;
}
static DEVICE_ATTR_RW(fn2_host_boot_en);

static ssize_t fn2_pci_vendor_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	ssize_t ret;

	regmap_read(drvdata->fn2_map, 0xf002c, &value);
	ret = sprintf(buf, "0x%04x", value);

	return ret;
}

static ssize_t fn2_pci_vendor_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(dev);
	int input;
	int rc;

	rc = kstrtoint(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	regmap_write(drvdata->fn2_map, 0xf002c, input);

	return count;
}
static DEVICE_ATTR_RW(fn2_pci_vendor);

static struct attribute *fn2_attrs[] = {
	&dev_attr_fn2_host_boot_en.attr,
	&dev_attr_fn2_pci_vendor.attr,
	NULL,
};

ATTRIBUTE_GROUPS(fn2);

static int sysfs_register(struct device *parent, struct gxp_fn2_drvdata *fn2)
{
	struct device *dev;

	dev = device_create_with_groups(soc_class, parent, 0,
					fn2, fn2_groups, "fn2");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	return 0;
}

static void gxp_fn2_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(chip->parent);
	unsigned int val;

	// Read latched interrupt
	regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);

	//Clear latched interrupt
	regmap_update_bits(drvdata->fn2_map, FN2_SEVSTAT,
			0xFFFF, 0xFFFF);
}

static void gxp_fn2_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_fn2_drvdata *drvdata = dev_get_drvdata(chip->parent);

	regmap_update_bits(drvdata->fn2_map, FN2_SEVMASK,
			BIT(0), set == true ? BIT(0):0);
}

static void gxp_fn2_gpio_irq_mask(struct irq_data *d)
{
	gxp_fn2_gpio_irq_set_mask(d, false);
}

static void gxp_fn2_gpio_irq_unmask(struct irq_data *d)
{
	gxp_fn2_gpio_irq_set_mask(d, true);
}

static int gxp_fn2_gpio_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static irqreturn_t gxp_fn2_irq_handle(int irq, void *_drvdata)
{
	struct gxp_fn2_drvdata *drvdata = (struct gxp_fn2_drvdata *)_drvdata;
	unsigned int val, girq;

	//handle system event
	val = readb(drvdata->base + FN2_SEVSTAT);

	if (val & PGOOD_MASK) {
		girq = irq_find_mapping(drvdata->gpio_chip.irq.domain, PGOOD);
		generic_handle_irq(girq);
	}
/*
 *	if (val & PERST_MASK) {
 *		girq = irq_find_mapping(drvdata->gpio_chip.irq.domain, PERST);
 *		generic_handle_irq(girq);
 *	}
 */
	return IRQ_HANDLED;
}

static const struct gpio_chip fn2_chip = {
	.label			= "gxp-fn2",
	.owner			= THIS_MODULE,
	.get			= gxp_fn2_gpio_get,
	.set			= gxp_fn2_gpio_set,
	.get_direction = gxp_fn2_gpio_get_direction,
	.direction_input = gxp_fn2_gpio_direction_input,
	.direction_output = gxp_fn2_gpio_direction_output,
	.base = -1,
	//.can_sleep		= true,
};

static struct irq_chip gxp_gpio_irqchip = {
	.name		= "gxp-fn2",
	.irq_ack	= gxp_fn2_gpio_irq_ack,
	.irq_mask	= gxp_fn2_gpio_irq_mask,
	.irq_unmask	= gxp_fn2_gpio_irq_unmask,
	.irq_set_type	= gxp_fn2_gpio_set_type,
	.flags          = IRQCHIP_IMMUTABLE,
};

static const struct of_device_id gxp_fn2_of_match[] = {
	{ .compatible = "hpe,gxp-fn2" },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_fn2_of_match);

static int gxp_fn2_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_fn2_drvdata *drvdata;
	struct resource *res;
	struct gpio_irq_chip *girq;

	gxp_pgood_trigger = 0;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_fn2_drvdata),
				GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->fn2_map = syscon_regmap_lookup_by_compatible("hpe,gxp-fn2");
	if (IS_ERR(drvdata->fn2_map)) {
		dev_err(&pdev->dev, "Unable to find fn2 regmap\n");
		return PTR_ERR(drvdata->fn2_map);
	}

	drvdata->xreg_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"xreg_handle");
	if (IS_ERR(drvdata->xreg_map)) {
		dev_err(&pdev->dev, "failed to map xreg_handle\n");
		return -ENODEV;
	}

	drvdata->gpio_chip = fn2_chip;
	drvdata->gpio_chip.ngpio = 4;
	drvdata->gpio_chip.parent = &pdev->dev;

	girq = &drvdata->gpio_chip.irq;
	girq->chip = &gxp_gpio_irqchip;
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;

	// Set up interrupt from fn2 system event reg

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Get irq from platform fail - %d\n", ret);
		return ret;
	}
	drvdata->irq = ret;

	drvdata->host_boot_en_called = 0;

	ret = devm_request_irq(&pdev->dev, drvdata->irq, gxp_fn2_irq_handle,
							IRQF_SHARED, "gxp-fn2", drvdata);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ handler failed - %d\n", ret);
		return ret;
	}

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "Could not register gpiochip for fn2, %d\n", ret);
	dev_info(&pdev->dev, "HPE GXP FN2 driver loaded.\n");

	ret = sysfs_register(&pdev->dev, drvdata);
	if (ret < 0) {
		dev_warn(&pdev->dev, "Unable to register sysfs\n");
		return ret;
	}

	return 0;
}

static struct platform_driver gxp_fn2_driver = {
	.probe = gxp_fn2_probe,
	.driver = {
		.name = "gxp-fn2",
		.of_match_table = of_match_ptr(gxp_fn2_of_match),
	},
};
module_platform_driver(gxp_fn2_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_AUTHOR("Jorge Cisneros <jorge.cisneros@hpe.com>");
MODULE_DESCRIPTION("HPE GXP FN2 Driver");

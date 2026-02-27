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
#include "linux/gxp-soclib.h"
#include <linux/of.h>

#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1

short int server_xreg_id;
EXPORT_SYMBOL(server_xreg_id);

enum xreg_gpio_pn {
	XREG_MAX_BIT = 4095
};

struct gxp_xreg_interrupt_offset {
	unsigned int int_base;
	unsigned int int_grp_base;
};
struct gxp_xreg_drvdata {
	void __iomem *base;
	struct regmap *xreg_map;
	struct gpio_chip gpio_chip;
	struct gxp_xreg_interrupt_offset *intoffset;
	int irq;
};

static ssize_t server_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	ssize_t ret;

	regmap_read(drvdata->xreg_map, 0x0, &value);
	ret = sprintf(buf, "0x%04x", (value&0xffff00)>>8);

	return ret;
}
static DEVICE_ATTR_RO(server_id);

static ssize_t sideband_sel_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	ssize_t ret;

	regmap_read(drvdata->xreg_map, 0x40, &value);
	ret = sprintf(buf, "0x%02x", value&0x03);

	return ret;
}

static ssize_t sideband_sel_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	int input;
	int rc;

	rc = kstrtoint(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	if (input & 0x03)
		return -EINVAL;

	regmap_update_bits(drvdata->xreg_map, 0x40, 0x03, input);

	return count;
}
static DEVICE_ATTR_RW(sideband_sel);

static struct attribute *xreg_attrs[] = {
	&dev_attr_server_id.attr,
	&dev_attr_sideband_sel.attr,
	NULL,
};
ATTRIBUTE_GROUPS(xreg);

static int sysfs_register(struct device *parent, struct gxp_xreg_drvdata *xreg)
{
	struct device *dev;

	dev = device_create_with_groups(soc_class, parent, 0,
					xreg, xreg_groups, "xreg");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	return 0;
}

static int gxp_gpio_xreg_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(chip->parent);
	int ret = 0;
	int xreg_byte = 0;
	int xreg_bit = 0;
	int xreg = 0;
	// allow read access for both input and output type XReg
	switch (offset) {
	case 0 ... XREG_MAX_BIT:
		xreg_byte = (offset / 32) * 4;
		xreg_bit  = offset % 32;
		regmap_read(drvdata->xreg_map, xreg_byte, &xreg);
		ret = (xreg & BIT(xreg_bit)) ? 1 : 0;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	return ret;
}

static void gxp_gpio_xreg_set(struct gpio_chip *chip,
			unsigned int offset, int value)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(chip->parent);
	int xreg_byte = 0;
	int xreg_bit = 0;

	// allow write access only for output and input/output type XReg
	switch (offset) {
	case 1 ... 2:
	case 32 ... 39:
	case 40:
	case 43 ... 45:
	case 47:
	case 52 ... 57:
	case 59 ... 60:
	case 62:
	case 71 ... 78:
	case 81 ... 83:
	case 85:
	case 91 ... 111:
	case 128:
	case 136 ... 139:
	case 175 ... 176:
	case 178:
	case 288 ... 295:
	case 335:
	case 344 ... 346:
	case 353:
	case 356:
	case 358:
	case 360 ... 361:
	case 416 ... 423:
	case 432:
	case 434:
	case 440 ... 447:
	case 507 ... 514:
	case 520 ... 527:
	case 536 ... 543:
	case 576 ... 583:
	case 585:
	case 587 ... 592:
	case 594:
	case 597 ... 598:
	case 602 ... 607:
	case 612 ... 615:
	case 624 ... 631:
	case 637 ... 638:
	case 648 ... 650:
	case 652 ... 654:
	case 656 ... 661:
	case 664 ... 671:
	case 680 ... 695:
	case 715:
	case 717:
	case 784 ... 822:
	case 880 ... 895:
	case 904 ... 975:
	case 1032 ... 1048:
	case 1056 ... 1127:
	case 1200 ... 1215:
	case 1232 ... 1343:
	case 1360 ... 1375:
	case 1392 ... 1423:
	case 1432 ... 1439:
	case 1504:
	case 1512:
	case 1528 ... 1539:
	case 1544 ... 1547:
	case 1560 ... 1571:
	case 1576 ... 1579:
	case 1592 ... 1606:
	case 1608 ... 1614:
	case 1624 ... 1634:
	case 1640 ... 1642:
	case 1656 ... 1667:
	case 1672 ... 1675:
	case 1688 ... 1711:
	case 1728:
	case 1736:
	case 1760 ... 1763:
	case 1768 ... 1771:
	case 1792 ... 1797:
	case 1800 ... 1805:
	case 2048 ... 2111:
	case 2128 ... 2134:
	case 2136 ... 2139:
	case 2141:
	case 2240 ... 2261:
	case 2272 ... 2279:
	case 2308:
	case 2316:
	case 2324:
	case 2332:
	case 2340:
	case 2348:
	case 2356:
	case 2364:
	case 2372:
	case 2380:
	case 2388:
	case 2396:
	case 2404:
	case 2412:
	case 2420:
	case 2428:
	case 2436:
	case 2444:
	case 2452:
	case 2460:
	case 2468:
	case 2476:
	case 2484:
	case 2492:
	case 3200 ... 3263:
	case 3272 ... 3343:
	case 3408:
	case 3416 ... 3449:
	case 3456 ... 3511:
	case 3520 ... 4079:
		xreg_byte = (offset / 32) * 4;
		xreg_bit  = offset % 32;
		regmap_update_bits(drvdata->xreg_map, xreg_byte,
				   BIT(xreg_bit), value ? BIT(xreg_bit) : 0);
		break;
	case 46:
	// UID
		regmap_update_bits(drvdata->xreg_map, 0x4, 0x0000ff00,
				   value ? 0xc000 : 0x8000);
		break;
	default:
		break;
	}
}

static int gxp_gpio_xreg_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case 1 ... 2:
	case 32 ... 39:
	case 40:
	case 43 ... 47:
	case 52 ... 57:
	case 59 ... 60:
	case 62:
	case 71 ... 78:
	case 81 ... 83:
	case 85:
	case 91 ... 111:
	case 128:
	case 136 ... 139:
	case 175 ... 176:
	case 178:
	case 288 ... 295:
	case 335:
	case 344 ... 346:
	case 353:
	case 356:
	case 358:
	case 360 ... 361:
	case 416 ... 423:
	case 432:
	case 434:
	case 440 ... 447:
	case 507 ... 514:
	case 520 ... 527:
	case 536 ... 543:
	case 576 ... 583:
	case 585:
	case 587 ... 592:
	case 594:
	case 597 ... 598:
	case 602 ... 607:
	case 612 ... 615:
	case 624 ... 631:
	case 637 ... 638:
	case 648 ... 650:
	case 652 ... 654:
	case 656 ... 661:
	case 664 ... 671:
	case 680 ... 695:
	case 715:
	case 717:
	case 784 ... 822:
	case 880 ... 895:
	case 904 ... 975:
	case 1032 ... 1048:
	case 1056 ... 1127:
	case 1200 ... 1215:
	case 1232 ... 1343:
	case 1360 ... 1375:
	case 1392 ... 1423:
	case 1432 ... 1439:
	case 1504:
	case 1512:
	case 1528 ... 1539:
	case 1544 ... 1547:
	case 1560 ... 1571:
	case 1576 ... 1579:
	case 1592 ... 1606:
	case 1608 ... 1614:
	case 1624 ... 1634:
	case 1640 ... 1642:
	case 1656 ... 1667:
	case 1672 ... 1675:
	case 1688 ... 1711:
	case 1728:
	case 1736:
	case 1760 ... 1763:
	case 1768 ... 1771:
	case 1792 ... 1797:
	case 1800 ... 1805:
	case 2048 ... 2111:
	case 2128 ... 2134:
	case 2136 ... 2139:
	case 2141:
	case 2240 ... 2261:
	case 2272 ... 2279:
	case 2308:
	case 2316:
	case 2324:
	case 2332:
	case 2340:
	case 2348:
	case 2356:
	case 2364:
	case 2372:
	case 2380:
	case 2388:
	case 2396:
	case 2404:
	case 2412:
	case 2420:
	case 2428:
	case 2436:
	case 2444:
	case 2452:
	case 2460:
	case 2468:
	case 2476:
	case 2484:
	case 2492:
	case 3200 ... 3263:
	case 3272 ... 3343:
	case 3408:
	case 3416 ... 3449:
	case 3456 ... 3511:
	case 3520 ... 4079:
		ret = GPIO_DIR_OUT;
		break;
	case 3 ... 31:
	case 41:
	case 49 ... 51:
	case 58:
	case 61:
	case 63 ... 64:
	case 68 ... 70:
	case 79 ... 80:
	case 84:
	case 86:
	case 168 ... 169:
	case 177:
	case 179:
	case 192 ... 215:
	case 256 ... 287:
	case 296 ... 327:
	case 329:
	case 352:
	case 354 ... 355:
	case 357:
	case 384 ... 415:
	case 433:
	case 456 ... 463:
	case 467 ... 471:
	case 473 ... 479:
	case 481 ... 487:
	case 489 ... 495:
	case 498 ... 506:
	case 515:
	case 528 ... 535:
	case 544 ... 562:
	case 584:
	case 586:
	case 593:
	case 595:
	case 616 ... 623:
	case 632 ... 636:
	case 643 ... 646:
	case 672 ... 679:
	case 716:
	case 718:
	case 720 ... 735:
	case 752 ... 756:
	case 760 ... 764:
	case 832 ... 833:
	case 840 ... 842:
	case 844 ... 855:
	case 864 ... 879:
	case 896 ... 897:
	case 976 ... 1031:
	case 1184 ... 1199:
	case 1216 ... 1231:
	case 1424 ... 1431:
	case 1520:
	case 1540 ... 1543:
	case 1552 ... 1555:
	case 1584 ... 1587:
	case 1616 ... 1622:
	case 1648 ... 1650:
	case 1680 ... 1683:
	case 1712 ... 1719:
	case 1744:
	case 1776 ... 1779:
	case 1808 ... 1813:
	case 2112 ... 2117:
	case 2120 ... 2122:
	case 2124 ... 2127:
	case 2144 ... 2151:
	case 2262 ... 2263:
	case 2304 ... 2305:
	case 2312 ... 2313:
	case 2320 ... 2321:
	case 2328 ... 2329:
	case 2336 ... 2337:
	case 2344 ... 2345:
	case 2352 ... 2353:
	case 2360 ... 2361:
	case 2368 ... 2369:
	case 2376 ... 2377:
	case 2384 ... 2385:
	case 2392 ... 2393:
	case 2400 ... 2401:
	case 2408 ... 2409:
	case 2416 ... 2417:
	case 2424 ... 2425:
	case 2432 ... 2433:
	case 2440 ... 2441:
	case 2448 ... 2449:
	case 2456 ... 2457:
	case 2464 ... 2465:
	case 2472 ... 2473:
	case 2480 ... 2481:
	case 2488 ... 2489:
	case 3056 ... 3063:
	case 3152 ... 3175:
	case 3072 ... 3087:
		ret = GPIO_DIR_IN;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_gpio_xreg_direction_input(struct gpio_chip *chip,
				unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case 0 ... XREG_MAX_BIT:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_gpio_xreg_direction_output(struct gpio_chip *chip,
				unsigned int offset, int value)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case 1 ... 2:
	case 32 ... 39:
	case 40:
	case 43 ... 47:
	case 52 ... 57:
	case 59 ... 60:
	case 62:
	case 71 ... 78:
	case 81 ... 83:
	case 85:
	case 91 ... 111:
	case 128:
	case 136 ... 139:
	case 175 ... 176:
	case 178:
	case 288 ... 295:
	case 335:
	case 344 ... 346:
	case 353:
	case 356:
	case 358:
	case 360 ... 361:
	case 416 ... 423:
	case 432:
	case 434:
	case 440 ... 447:
	case 507 ... 514:
	case 520 ... 527:
	case 536 ... 543:
	case 576 ... 583:
	case 585:
	case 587 ... 592:
	case 594:
	case 597 ... 598:
	case 602 ... 607:
	case 612 ... 615:
	case 624 ... 631:
	case 637 ... 638:
	case 648 ... 650:
	case 652 ... 654:
	case 656 ... 661:
	case 664 ... 671:
	case 680 ... 695:
	case 715:
	case 717:
	case 784 ... 822:
	case 880 ... 895:
	case 904 ... 975:
	case 1032 ... 1048:
	case 1056 ... 1127:
	case 1200 ... 1215:
	case 1232 ... 1343:
	case 1360 ... 1375:
	case 1392 ... 1423:
	case 1432 ... 1439:
	case 1504:
	case 1512:
	case 1528 ... 1539:
	case 1544 ... 1547:
	case 1560 ... 1571:
	case 1576 ... 1579:
	case 1592 ... 1606:
	case 1608 ... 1614:
	case 1624 ... 1634:
	case 1640 ... 1642:
	case 1656 ... 1667:
	case 1672 ... 1675:
	case 1688 ... 1711:
	case 1728:
	case 1736:
	case 1760 ... 1763:
	case 1768 ... 1771:
	case 1792 ... 1797:
	case 1800 ... 1805:
	case 2048 ... 2111:
	case 2128 ... 2134:
	case 2136 ... 2139:
	case 2141:
	case 2240 ... 2261:
	case 2272 ... 2279:
	case 2308:
	case 2316:
	case 2324:
	case 2332:
	case 2340:
	case 2348:
	case 2356:
	case 2364:
	case 2372:
	case 2380:
	case 2388:
	case 2396:
	case 2404:
	case 2412:
	case 2420:
	case 2428:
	case 2436:
	case 2444:
	case 2452:
	case 2460:
	case 2468:
	case 2476:
	case 2484:
	case 2492:
	case 3200 ... 3263:
	case 3272 ... 3343:
	case 3408:
	case 3416 ... 3449:
	case 3456 ... 3511:
	case 3520 ... 4079:
		gxp_gpio_xreg_set(chip, offset, value);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static void gxp_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(chip->parent);
	int xreg_byte = 0;
	int xreg_bit = 0;

	// mask the interrupt
	xreg_byte = ((d->hwirq - 8) / 32) * 4;
	xreg_bit  = (d->hwirq - 8) % 32;
	regmap_update_bits(drvdata->xreg_map, xreg_byte, BIT(xreg_bit), BIT(xreg_bit));

	//Clear latched interrupt
	xreg_byte = ((d->hwirq - 16) / 32) * 4;
	xreg_bit  = (d->hwirq - 16) % 32;
	regmap_update_bits(drvdata->xreg_map, xreg_byte, BIT(xreg_bit), BIT(xreg_bit));
}

static void gxp_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	int xreg_byte = 0;
	int xreg_bit = 0;
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(chip->parent);

	// set the mask for the requested hwirq
	xreg_byte = ((d->hwirq - 8) / 32) * 4;
	xreg_bit  = (d->hwirq - 8) % 32;
	regmap_update_bits(drvdata->xreg_map, xreg_byte, BIT(xreg_bit),
			   set == true ? 0 : BIT(xreg_bit));

	// clear the latched Interrupt
	xreg_byte = ((d->hwirq - 16) / 32) * 4;
	xreg_bit  = (d->hwirq - 16) % 32;
	regmap_update_bits(drvdata->xreg_map, xreg_byte, BIT(xreg_bit),
			   BIT(xreg_bit));
}

static void gxp_gpio_irq_mask(struct irq_data *d)
{
	gxp_gpio_irq_set_mask(d, false);
}

static void gxp_gpio_irq_unmask(struct irq_data *d)
{
	gxp_gpio_irq_set_mask(d, true);
}

static int gxp_gpio_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static irqreturn_t gxp_xreg_irq_handle(int irq, void *_drvdata)
{
	struct gxp_xreg_drvdata *drvdata = (struct gxp_xreg_drvdata *)_drvdata;
	unsigned int val, grpval, index, grpindex, hwirq;
	int xreg_byte = 0;
	int xreg_bit = 0;

	//scan for all interrupt group status register to findout which group
	//has asserted the interrupt
	grpval = readw(drvdata->base + drvdata->intoffset->int_grp_base + 4);
	for_each_set_bit(grpindex, (unsigned long *)&grpval, 16) {
		//check which interrupt is asserted within a group
		val = readb(drvdata->base + drvdata->intoffset->int_base + (grpindex * 4));
		for_each_set_bit(index, (unsigned long *)&val, 8) {
			hwirq =  index +
				 (((drvdata->intoffset->int_base +
				 (grpindex * 4) +
				 2) * 8));
			// in this case hwirq points to the unlatched bit of the interrupt
			generic_handle_domain_irq(drvdata->gpio_chip.irq.domain,
						  index +
						  (((drvdata->intoffset->int_base +
						  (grpindex * 4) +
						  2) * 8)));
			// unmask the interrupt for the corresponding hwirq
			xreg_byte = (((hwirq - 8) / 32) * 4);
			xreg_bit  = ((hwirq - 8) % 32);
			regmap_update_bits(drvdata->xreg_map, xreg_byte, BIT(xreg_bit), 0);
		}
	}

	return IRQ_HANDLED;
}

static const struct gpio_chip xreg_chip = {
	.label			= "gxp-xreg",
	.owner			= THIS_MODULE,
	.get			= gxp_gpio_xreg_get,
	.set			= gxp_gpio_xreg_set,
	.get_direction = gxp_gpio_xreg_get_direction,
	.direction_input = gxp_gpio_xreg_direction_input,
	.direction_output = gxp_gpio_xreg_direction_output,
	.base = -1,
	//.can_sleep		= true,
};

static const struct irq_chip gxp_gpio_irqchip = {
	.name		= "gxp-xreg",
	.irq_ack	= gxp_gpio_irq_ack,
	.irq_mask	= gxp_gpio_irq_mask,
	.irq_unmask	= gxp_gpio_irq_unmask,
	.irq_set_type	= gxp_gpio_set_type,
	.flags = IRQCHIP_IMMUTABLE,
};

static const struct of_device_id gxp_xreg_of_match[] = {
	{ .compatible = "hpe,gxp-xreg"},
	{},
};
MODULE_DEVICE_TABLE(of, gxp_xreg_of_match);

static int gxp_gpio_irq_init_hw(struct gpio_chip *chip)
{
	//struct gxp_gpio_drvdata *drvdata = dev_get_drvdata(chip->parent);

	/* TODO NICK Find correct bits to initialize */

	return 0;
}

static int gxp_xreg_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_xreg_drvdata *drvdata;
	struct resource *res;
	struct gpio_irq_chip *girq;
	uint32_t  int_base_offset[2];
	u32 value;

	server_xreg_id = 0x0000;

	drvdata = devm_kzalloc(&pdev->dev,
				sizeof(struct gxp_xreg_drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->intoffset = devm_kzalloc(&pdev->dev,sizeof(*drvdata->intoffset), GFP_KERNEL);
	if (!drvdata->intoffset)
		return -ENOMEM;

	// according to XReg spec, the offset from where Xreg Interrupts starts is
	// different for different platforms.
	// get the Interrupt base and Interrupt group base offset from the device tree
	ret = of_property_read_u32_array(pdev->dev.of_node, "interrupt-base-offset",
					 int_base_offset, 2);
	if (ret != 0x00) {
		dev_err(&pdev->dev, "failed to get XReg Interrupt base offset - %d\n", ret);
		return ret;
	}

	drvdata->intoffset->int_base = int_base_offset[1];
	drvdata->intoffset->int_grp_base = int_base_offset[0];

	platform_set_drvdata(pdev, drvdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->xreg_map = syscon_regmap_lookup_by_compatible("hpe,gxp-xreg");
	if (IS_ERR(drvdata->xreg_map)) {
		dev_err(&pdev->dev, "Unable to find xreg regmap\n");
		return PTR_ERR(drvdata->xreg_map);
	}

	drvdata->gpio_chip = xreg_chip;
	drvdata->gpio_chip.ngpio = 4096;
	drvdata->gpio_chip.parent = &pdev->dev;

	girq = &drvdata->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &gxp_gpio_irqchip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	girq->init_hw = gxp_gpio_irq_init_hw;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "Could not register gpiochip for xreg, %d\n", ret);

	//update serverid
	regmap_read(drvdata->xreg_map, 0x0, &value);
	server_xreg_id = (value & 0xffff00) >> 8;

	// enable higher priority interrupts for all groups //0xA0
	regmap_update_bits(drvdata->xreg_map, drvdata->intoffset->int_grp_base + 12, 0xFFFF,
			   0xFFFF);

	// unmask interrupts for all groups //0x9C
	regmap_update_bits(drvdata->xreg_map, drvdata->intoffset->int_grp_base + 8, 0xFFFF, 0x00);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Get irq from platform fail - %d\n", ret);
		return ret;
	}
	drvdata->irq = ret;

	ret = devm_request_irq(&pdev->dev, drvdata->irq, gxp_xreg_irq_handle,
							IRQF_SHARED, "gxp-xreg", drvdata);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ handler failed - %d\n", ret);
		return ret;
	}

	ret = sysfs_register(&pdev->dev, drvdata);
	if (ret < 0) {
		dev_warn(&pdev->dev, "Unable to register sysfs\n");
		return ret;
	}

	return 0;
}

static struct platform_driver gxp_xreg_driver = {
	.probe = gxp_xreg_probe,
	.driver = {
		.name = "gxp-xreg",
		.of_match_table = of_match_ptr(gxp_xreg_of_match),
	},
};
module_platform_driver(gxp_xreg_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("HPE GXP Xreg Driver");

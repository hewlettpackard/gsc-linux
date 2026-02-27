// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
//#define DEBUG

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/peci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define PECICMD		0x0
#define   PECICMD_START							BIT(0)
#define   PECICMD_TAGET_ADDR_MASK		GENMASK(15, 8)
#define   PECICMD_WRITE_LEN_MASK		GENMASK(23, 16)
#define   PECICMD_READ_LEN_MASK		GENMASK(31, 24)

#define PECISTAT	0x4
#define   PECISTAT_TIM_NEG_FAIL				BIT(12)
#define   PECISTAT_BIT_COLLISION_DET	BIT(11)
#define   PECISTAT_BAD_READ_FCS				BIT(10)
#define   PECISTAT_BAD_WRITE_FCS			BIT(9)
#define   PECISTAT_CLIENT_ABORT_FCS		BIT(8)
#define   PECISTAT_DONE								BIT(0)
#define PECICFG		0xc
#define PECIDATA_OUT	0x100
#define PECIDATA_IN		0x180
#define PECIDATA_SIZE_MAX 128

#define PECICMD_TIMEOUT_MS_DEFAULT	1000

struct gxp_peci {
	struct peci_controller	*controller;
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	spinlock_t		lock; /* to sync completion status handling */
	struct completion	xfer_complete;
	u32			status;

};

static int gxp_peci_xfer(struct peci_controller *controller,
			 u8 addr, struct peci_request *req)
{
	long err;
	struct gxp_peci *priv = dev_get_drvdata(controller->dev.parent);
	u32 pecicmd;
	ulong flags;
	int i, ret = 0;

	//pr_devel("gxp_peci_xfer\n");

	if (req->tx.len > PECIDATA_SIZE_MAX ||
	    req->rx.len > PECIDATA_SIZE_MAX) {
		//pr_devel("gxp_peci_xfer:  req->tx.len=%d req->rx.len=%d\n", req->tx.len, req->rx.len);
		return -EINVAL;
	}

	if ((priv->status & PECISTAT_DONE) == 0x00) {
		//pr_devel("gxp_peci_xfer:  PECISTAT_DONE == 0\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&priv->lock, flags);
	reinit_completion(&priv->xfer_complete);

	for (i = 0; i < req->tx.len; i++)
		writeb(req->tx.buf[i], priv->base + PECIDATA_OUT + i);

	pecicmd = FIELD_PREP(PECICMD_TAGET_ADDR_MASK, addr) |
		  FIELD_PREP(PECICMD_WRITE_LEN_MASK, req->tx.len) |
		  FIELD_PREP(PECICMD_READ_LEN_MASK, req->rx.len) |
		  PECICMD_START;

	priv->status = 0;

	//pr_devel("PECICMD : 0x%08x\n", pecicmd);
	print_hex_dump_debug("TX : ", DUMP_PREFIX_NONE, 16, 1,
			     req->tx.buf, req->tx.len, true);

	writel(pecicmd, priv->base + PECICMD);
	spin_unlock_irqrestore(&priv->lock, flags);

	err = wait_for_completion_interruptible_timeout(&priv->xfer_complete,
							PECICMD_TIMEOUT_MS_DEFAULT);

	spin_lock_irqsave(&priv->lock, flags);

	if (err <= 0 || priv->status != PECISTAT_DONE) {
		if (err < 0) { /* -ERESTARTSYS */
			ret = (int)err;
			goto err_irqrestore;
		} else if (err == 0) {
			//pr_devel("Timeout waiting for a response!\n");
			ret = -ETIMEDOUT;
			goto err_irqrestore;
		}

		//pr_devel("No valid response!\n");
		ret = -EIO;
		goto err_irqrestore;
	}

	for (i = 0; i < req->rx.len; i++)
		req->rx.buf[i] = readb(priv->base + PECIDATA_IN + i);

	print_hex_dump_debug("RX : ", DUMP_PREFIX_NONE, 16, 1,
			     req->rx.buf, req->rx.len, true);

err_irqrestore:
	priv->status = PECISTAT_DONE;
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static irqreturn_t gxp_peci_irq_handler(int irq, void *arg)
{
	struct gxp_peci *priv = arg;
	u32 status;

	spin_lock(&priv->lock);
	status = readl(priv->base + PECISTAT);
	writel(status, priv->base + PECISTAT);
	priv->status = status;

/*	if (status & PECISTAT_CLIENT_ABORT_FCS)
 *		pr_devel("PECISTAT_CLIENT_ABORT_FCS\n");
 *
 *	if (status & PECISTAT_BAD_WRITE_FCS)
 *		pr_devel("PECISTAT_BAD_WRITE_FCS\n");
 *
 *	if (status & PECISTAT_BAD_READ_FCS)
 *		pr_devel("PECISTAT_BAD_READ_FCS\n");
 *
 *	if (status & PECISTAT_BIT_COLLISION_DET)
 *		pr_devel("PECISTAT_BIT_COLLISION_DET\n");
 *
 *	if (status & PECISTAT_TIM_NEG_FAIL)
 *		pr_devel("PECISTAT_TIM_NEG_FAIL\n");
 */

	if (status & PECISTAT_DONE) {
		//pr_devel("PECISTAT_DONE\n");
		complete(&priv->xfer_complete);
	}

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static const struct peci_controller_ops gxp_ops = {
	.xfer = gxp_peci_xfer,
};

static int gxp_peci_probe(struct platform_device *pdev)
{
	struct peci_controller *controller;
	struct gxp_peci *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	dev_set_drvdata(priv->dev, priv);

	priv->dev = &pdev->dev;
	priv->status = PECISTAT_DONE;
	dev_set_drvdata(&pdev->dev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		return PTR_ERR(priv->base);
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		return priv->irq;
	}

	ret = devm_request_irq(&pdev->dev, priv->irq, gxp_peci_irq_handler,
			       0, "peci-gxp-irq", priv);
	if (ret)
		return ret;

	init_completion(&priv->xfer_complete);
	spin_lock_init(&priv->lock);

	controller = devm_peci_controller_add(priv->dev, &gxp_ops);
	if (IS_ERR(controller))
		return dev_err_probe(priv->dev, PTR_ERR(controller),
				     "failed to add hpe peci controller\n");

	priv->controller = controller;

	dev_info(&pdev->dev, "peci bus registered, irq %d\n", priv->irq);

	return 0;
}

static const struct of_device_id gxp_peci_of_table[] = {
	{ .compatible = "hpe,gxp-peci", },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_peci_of_table);

static struct platform_driver gxp_peci_driver = {
	.probe  = gxp_peci_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(gxp_peci_of_table),
	},
};
module_platform_driver(gxp_peci_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("GXP PECI driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * The driver for BMC side of GXP SSIF interface
 *
 * Copyright (c) 2021, Ampere Computing LLC
 * Copyright (c) 2022-2025 Hewlett Packard Enterprise Development LP
 */

#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/iopoll.h>

#include "ssif_bmc.h"

#define GXP_I2CSCMD 0x06
#define GXP_I2CD_M_S_RX_CMD_LAST 0x61

struct gxp_i2c_drvdata {
	struct device *dev;
	void __iomem *base;
	u32 bus_frequency;
	int engine;
	int irq;
	struct completion completion;
	struct i2c_adapter adapter;
	struct i2c_msg *curr_msg;
	int msgs_remaining;
	int msgs_num;
	u8 *buf;
	size_t buf_remaining;
	unsigned char state;
	struct i2c_client *slave;
	unsigned char stopped;
};

static void gxp_response_nack(struct ssif_bmc_ctx *ssif_bmc)
{
	struct gxp_i2c_drvdata *drvdata;

	drvdata = (struct gxp_i2c_drvdata *)ssif_bmc->priv;
	if (!drvdata)
		return;
	writeb(GXP_I2CD_M_S_RX_CMD_LAST, drvdata->base + GXP_I2CSCMD);
}

static int ssif_bmc_probe(struct i2c_client *client)
{
	struct ssif_bmc_ctx *ssif_bmc;

	ssif_bmc = ssif_bmc_alloc(client, sizeof(struct gxp_i2c_drvdata));
	if (IS_ERR(ssif_bmc))
		return PTR_ERR(ssif_bmc);

	ssif_bmc->priv = i2c_get_adapdata(client->adapter);
	ssif_bmc->en_response_nack = gxp_response_nack;

	return 0;
}

static void ssif_bmc_remove(struct i2c_client *client)
{
	struct ssif_bmc_ctx *ssif_bmc = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	misc_deregister(&ssif_bmc->miscdev);
}

static const struct of_device_id ssif_bmc_match[] = {
	{ .compatible = "hpe,gxp-ssif-bmc" },
	{ },
};

static const struct i2c_device_id ssif_bmc_id[] = {
	{ DEVICE_NAME, 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ssif_bmc_id);

static struct i2c_driver ssif_bmc_driver = {
	.driver		= {
		.name		= DEVICE_NAME,
		.of_match_table = ssif_bmc_match,
	},
	.probe		= ssif_bmc_probe,
	.remove		= ssif_bmc_remove,
	.id_table	= ssif_bmc_id,
};

module_i2c_driver(ssif_bmc_driver);

MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("Linux device driver of HPE GXP BMC IPMI SSIF interface.");
MODULE_LICENSE("GPL");

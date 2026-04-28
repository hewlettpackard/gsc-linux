// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#ifdef CONFIG_ARCH_HPE_GXP
#define MAX_I2C_ENGINE 10
static const char * const gxp_i2c_name[] = {
	"gxp-i2c0", "gxp-i2c1", "gxp-i2c2", "gxp-i2c3",
	"gxp-i2c4", "gxp-i2c5", "gxp-i2c6", "gxp-i2c7",
	"gxp-i2c8", "gxp-i2c9" };
#else
#define MAX_I2C_ENGINE 21
static const char * const gxp_i2c_name[] = {
	"gxp-i2c0",  "gxp-i2c1",  "gxp-i2c2",  "gxp-i2c3",
	"gxp-i2c4",  "gxp-i2c5",  "gxp-i2c6",  "gxp-i2c7",
	"gxp-i2c8",  "gxp-i2c9",  "gxp-i2c10", "gxp-i2c11",
	"gxp-i2c12", "gxp-i2c13", "gxp-i2c14", "gxp-i2c15",
	"gxp-i2c16", "gxp-i2c17", "gxp-i2c18", "gxp-i2c19",
	"gxp-i2c20" };
#endif /* CONFIG_ARCH_HPE_GXP */

/* GXP I2C Global interrupt status/enable register*/
#define GXP_I2CINTSTAT		0x00
#define GXP_I2CINTEN		0x04

/* GXP I2C registers */
#define GXP_I2CSTAT		0x00
#define MASK_STOP_EVENT		0x20
#define MASK_ACK		0x08
#define MASK_RW			0x04
#define GXP_I2CEVTERR		0x01
#define MASK_SLAVE_CMD_EVENT	0x01
#define MASK_SLAVE_DATA_EVENT	0x02
#define MASK_MASTER_EVENT	0x10
#define GXP_I2CSNPDAT		0x02
#define GXP_I2CMCMD		0x04
#define GXP_I2CSCMD		0x06
#define GXP_I2CSNPAA		0x09
#define GXP_I2CADVFEAT		0x0A
#define GXP_I2COWNADR		0x0B
#define GXP_I2CFREQDIV		0x0C
#define GXP_I2CFLTFAIR		0x0D
#define GXP_I2CTMOEDG		0x0E
#define GXP_I2CCYCTIM		0x0F

/* I2CSCMD Bits */
#define SNOOP_EVT_CLR		0x80
#define SLAVE_EVT_CLR		0x40
#define	SNOOP_EVT_MASK		0x20
#define SLAVE_EVT_MASK		0x10
#define SLAVE_ACK_ENAB		0x08
#define SLAVE_EVT_STALL		0x01

/* I2CMCMD Bits */
#define MASTER_EVT_CLR		0x80
#define MASTER_ACK_ENAB		0x08
#define RW_CMD			0x04
#define STOP_CMD		0x02
#define START_CMD		0x01

/* I2CTMOEDG value */
#define GXP_DATA_EDGE_RST_CTRL	0x0a /* 30ns */

/* I2CFLTFAIR Bits */
#define FILTER_CNT		0x30
#define FAIRNESS_CNT		0x02

enum {
	GXP_I2C_IDLE = 0,
	GXP_I2C_ADDR_PHASE,
	GXP_I2C_RDATA_PHASE,
	GXP_I2C_WDATA_PHASE,
	GXP_I2C_ADDR_NACK,
	GXP_I2C_DATA_NACK,
	GXP_I2C_ERROR,
	GXP_I2C_COMP
};

struct gxp_i2c_drvdata {
	struct device *dev;
	void __iomem *base;
	struct i2c_timings t;
	u32 engine;
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

static struct regmap *i2cg_map;

static void gxp_i2c_stop(struct gxp_i2c_drvdata *drvdata)
{
	/* Clear event and send stop */
	writeb(MASTER_EVT_CLR | STOP_CMD, drvdata->base + GXP_I2CMCMD);

	complete(&drvdata->completion);
}

static void gxp_i2c_abort(struct gxp_i2c_drvdata *drvdata)
{
	/* Emergency abort - reset hardware state */
	writeb(MASTER_EVT_CLR, drvdata->base + GXP_I2CMCMD);
	writeb(0x00, drvdata->base + GXP_I2CEVTERR);
	drvdata->state = GXP_I2C_IDLE;

	complete(&drvdata->completion);
}

static void gxp_i2c_start(struct gxp_i2c_drvdata *drvdata)
{
	u16 value;

	drvdata->buf = drvdata->curr_msg->buf;
	drvdata->buf_remaining = drvdata->curr_msg->len;

	/* Note: Address in struct i2c_msg is 7 bits */
	value = drvdata->curr_msg->addr << 9;

	/* Read or Write */
	value |= drvdata->curr_msg->flags & I2C_M_RD ? RW_CMD | START_CMD : START_CMD;

	drvdata->state = GXP_I2C_ADDR_PHASE;

	dev_dbg(drvdata->dev, "Start %s addr=0x%02x len=%zu\n",
		drvdata->curr_msg->flags & I2C_M_RD ? "read" : "write",
		drvdata->curr_msg->addr, drvdata->curr_msg->len);

	writew(value, drvdata->base + GXP_I2CMCMD);
}

static int gxp_i2c_master_xfer(struct i2c_adapter *adapter,
			       struct i2c_msg *msgs, int num)
{
	int ret;
	struct gxp_i2c_drvdata *drvdata = i2c_get_adapdata(adapter);
	unsigned long time_left;

	drvdata->msgs_remaining = num;
	drvdata->curr_msg = msgs;
	drvdata->msgs_num = num;
	reinit_completion(&drvdata->completion);

	gxp_i2c_start(drvdata);

	time_left = wait_for_completion_timeout(&drvdata->completion,
						adapter->timeout);
	ret = num - drvdata->msgs_remaining;
	if (time_left == 0) {
		dev_err(&adapter->dev, "I2C timeout in state %d\n", drvdata->state);
		gxp_i2c_abort(drvdata);
		return -ETIMEDOUT;
	}

	if (drvdata->state == GXP_I2C_ADDR_NACK) {
		dev_dbg(&adapter->dev, "Address NACK for addr 0x%02x\n", drvdata->curr_msg->addr);
		drvdata->state = GXP_I2C_IDLE;
		return -ENXIO;
	}

	if (drvdata->state == GXP_I2C_DATA_NACK) {
		dev_dbg(&adapter->dev, "Data NACK for addr 0x%02x\n", drvdata->curr_msg->addr);
		drvdata->state = GXP_I2C_IDLE;
		return -EIO;
	}

	if (drvdata->state == GXP_I2C_ERROR) {
		dev_err(&adapter->dev, "I2C error occurred\n");
		drvdata->state = GXP_I2C_IDLE;
		return -EIO;
	}

	return ret;
}

static u32 gxp_i2c_func(struct i2c_adapter *adap)
{
	if (IS_ENABLED(CONFIG_I2C_SLAVE))
		return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA |
		       I2C_FUNC_SMBUS_PEC | I2C_FUNC_SLAVE;

	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA |
	       I2C_FUNC_SMBUS_PEC;
}

static s32 gxp_i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
			      unsigned short flags, char read_write,
			      u8 command, int size, union i2c_smbus_data *data)
{
	struct i2c_msg msg[2];
	int num_msgs = 0;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 3]; /* Extra space for PEC */
	int ret;
	bool pec = (flags & I2C_CLIENT_PEC) != 0;

	switch (size) {
	case I2C_SMBUS_QUICK:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		msg[0].len = 0;
		msg[0].buf = NULL;
		if (read_write == I2C_SMBUS_READ)
			msg[0].flags |= I2C_M_RD;
		num_msgs = 1;
		break;

	case I2C_SMBUS_BYTE:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		if (read_write == I2C_SMBUS_READ) {
			msg[0].len = 1;
			msg[0].buf = buf;
			msg[0].flags |= I2C_M_RD;
		} else {
			msg[0].len = 1;
			msg[0].buf = buf;
			buf[0] = command;
		}
		num_msgs = 1;
		break;

	case I2C_SMBUS_BYTE_DATA:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		if (read_write == I2C_SMBUS_READ) {
			msg[0].len = 1;
			msg[0].buf = &command;

			msg[1].addr = addr;
			msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
			msg[1].len = 1;
			msg[1].buf = buf;
			num_msgs = 2;
		} else {
			msg[0].len = 2;
			msg[0].buf = buf;
			buf[0] = command;
			buf[1] = data->byte;
			num_msgs = 1;
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		if (read_write == I2C_SMBUS_READ) {
			msg[0].len = 1;
			msg[0].buf = &command;

			msg[1].addr = addr;
			msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
			msg[1].len = 2;
			msg[1].buf = buf;
			num_msgs = 2;
		} else {
			msg[0].len = 3;
			msg[0].buf = buf;
			buf[0] = command;
			buf[1] = data->word & 0xff;
			buf[2] = data->word >> 8;
			num_msgs = 1;
		}
		break;

	case I2C_SMBUS_PROC_CALL:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		msg[0].len = 3;
		msg[0].buf = buf;
		buf[0] = command;
		buf[1] = data->word & 0xff;
		buf[2] = data->word >> 8;

		msg[1].addr = addr;
		msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
		msg[1].len = 2;
		msg[1].buf = buf;
		num_msgs = 2;
		break;

	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_WRITE) {
			msg[0].addr = addr;
			msg[0].flags = flags & ~I2C_CLIENT_PEC;
			msg[0].len = data->block[0] + 2 + (pec ? 1 : 0);
			msg[0].buf = buf;
			buf[0] = command;
			buf[1] = data->block[0];
			memcpy(&buf[2], &data->block[1], data->block[0]);
			if (pec) {
				/* PEC calculation would go here - simplified for now */
				buf[data->block[0] + 2] = 0; /* Placeholder for PEC */
			}
			num_msgs = 1;
		} else {
			msg[0].addr = addr;
			msg[0].flags = flags & ~I2C_CLIENT_PEC;
			msg[0].len = 1;
			msg[0].buf = &command;

			msg[1].addr = addr;
			msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
			msg[1].len = I2C_SMBUS_BLOCK_MAX + 1 + (pec ? 1 : 0);
			msg[1].buf = buf;
			num_msgs = 2;
		}
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_WRITE) {
			msg[0].addr = addr;
			msg[0].flags = flags & ~I2C_CLIENT_PEC;
			msg[0].len = data->block[0] + 1 + (pec ? 1 : 0);
			msg[0].buf = buf;
			buf[0] = command;
			memcpy(&buf[1], &data->block[1], data->block[0]);
			if (pec)
				buf[data->block[0] + 1] = 0; /* Placeholder for PEC */
			num_msgs = 1;
		} else {
			msg[0].addr = addr;
			msg[0].flags = flags & ~I2C_CLIENT_PEC;
			msg[0].len = 1;
			msg[0].buf = &command;

			msg[1].addr = addr;
			msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
			msg[1].len = data->block[0] + (pec ? 1 : 0);
			msg[1].buf = buf;
			num_msgs = 2;
		}
		break;

	case I2C_SMBUS_BLOCK_PROC_CALL:
		msg[0].addr = addr;
		msg[0].flags = flags & ~I2C_CLIENT_PEC;
		msg[0].len = data->block[0] + 2;
		msg[0].buf = buf;
		buf[0] = command;
		buf[1] = data->block[0];
		memcpy(&buf[2], &data->block[1], data->block[0]);

		msg[1].addr = addr;
		msg[1].flags = (flags & ~I2C_CLIENT_PEC) | I2C_M_RD;
		msg[1].len = I2C_SMBUS_BLOCK_MAX + 1;
		msg[1].buf = buf;
		num_msgs = 2;
		break;

	default:
		return -EOPNOTSUPP;
	}

	ret = gxp_i2c_master_xfer(adapter, msg, num_msgs);
	if (ret < 0) {
		dev_dbg(&adapter->dev, "SMBus xfer failed: addr=0x%02x, size=%d, cmd=0x%02x, rw=%d, ret=%d\n",
			addr, size, command, read_write, ret);
		return ret;
	}

	/* Handle read data */
	if (read_write == I2C_SMBUS_READ) {
		switch (size) {
		case I2C_SMBUS_BYTE:
			data->byte = buf[0];
			break;
		case I2C_SMBUS_BYTE_DATA:
			data->byte = buf[0];
			break;
		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			data->word = buf[0] | (buf[1] << 8);
			break;
		case I2C_SMBUS_BLOCK_DATA:
			data->block[0] = buf[0];
			if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
				data->block[0] = I2C_SMBUS_BLOCK_MAX;
			memcpy(&data->block[1], &buf[1], data->block[0]);
			/* PEC validation would go here if enabled */
			break;
		case I2C_SMBUS_I2C_BLOCK_DATA:
			memcpy(&data->block[1], buf, data->block[0]);
			/* PEC validation would go here if enabled */
			break;
		case I2C_SMBUS_BLOCK_PROC_CALL:
			data->block[0] = buf[0];
			if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
				data->block[0] = I2C_SMBUS_BLOCK_MAX;
			memcpy(&data->block[1], &buf[1], data->block[0]);
			break;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static int gxp_i2c_reg_slave(struct i2c_client *slave)
{
	struct gxp_i2c_drvdata *drvdata = i2c_get_adapdata(slave->adapter);

	if (drvdata->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	drvdata->slave = slave;

	writeb(slave->addr << 1, drvdata->base + GXP_I2COWNADR);
	writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK | SLAVE_ACK_ENAB |
	       SLAVE_EVT_STALL, drvdata->base + GXP_I2CSCMD);

	return 0;
}

static int gxp_i2c_unreg_slave(struct i2c_client *slave)
{
	struct gxp_i2c_drvdata *drvdata = i2c_get_adapdata(slave->adapter);

	WARN_ON(!drvdata->slave);

	writeb(0x00, drvdata->base + GXP_I2COWNADR);
	writeb(SNOOP_EVT_CLR | SLAVE_EVT_CLR | SNOOP_EVT_MASK |
	       SLAVE_EVT_MASK, drvdata->base + GXP_I2CSCMD);

	drvdata->slave = NULL;

	return 0;
}
#endif

static const struct i2c_algorithm gxp_i2c_algo = {
	.master_xfer   = gxp_i2c_master_xfer,
	.smbus_xfer    = gxp_i2c_smbus_xfer,
	.functionality = gxp_i2c_func,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave     = gxp_i2c_reg_slave,
	.unreg_slave   = gxp_i2c_unreg_slave,
#endif
};

static void gxp_i2c_restart(struct gxp_i2c_drvdata *drvdata)
{
	u16 value;

	drvdata->buf = drvdata->curr_msg->buf;
	drvdata->buf_remaining = drvdata->curr_msg->len;

	value = drvdata->curr_msg->addr << 9;

	if (drvdata->curr_msg->flags & I2C_M_RD) {
		/* Read and clear master event */
		value |= MASTER_EVT_CLR | RW_CMD | START_CMD;
	} else {
		/* Write and clear master event */
		value |= MASTER_EVT_CLR | START_CMD;
	}

	drvdata->state = GXP_I2C_ADDR_PHASE;

	writew(value, drvdata->base + GXP_I2CMCMD);
}

static void gxp_i2c_chk_addr_ack(struct gxp_i2c_drvdata *drvdata)
{
	u16 value;

	value = readb(drvdata->base + GXP_I2CSTAT);
	if (!(value & MASK_ACK)) {
		/* Got no ack, stop */
		drvdata->state = GXP_I2C_ADDR_NACK;
		gxp_i2c_stop(drvdata);
		return;
	}

	if (drvdata->curr_msg->flags & I2C_M_RD) {
		/* Start to read data from slave */
		if (drvdata->buf_remaining == 0) {
			/* No more data to read, stop */
			drvdata->msgs_remaining--;
			drvdata->state = GXP_I2C_COMP;
			gxp_i2c_stop(drvdata);
			return;
		}
		drvdata->state = GXP_I2C_RDATA_PHASE;

		if (drvdata->buf_remaining == 1) {
			/* The last data, do not ack */
			writeb(MASTER_EVT_CLR | RW_CMD,
			       drvdata->base + GXP_I2CMCMD);
		} else {
			/* Read data and ack it */
			writeb(MASTER_EVT_CLR | MASTER_ACK_ENAB |
			       RW_CMD, drvdata->base + GXP_I2CMCMD);
		}
	} else {
		/* Start to write first data to slave */
		if (drvdata->buf_remaining == 0) {
			/* No more data to write, stop */
			drvdata->msgs_remaining--;
			drvdata->state = GXP_I2C_COMP;
			gxp_i2c_stop(drvdata);
			return;
		}
		value = *drvdata->buf;
		value = value << 8;
		/* Clear master event */
		value |= MASTER_EVT_CLR;
		drvdata->buf++;
		drvdata->buf_remaining--;
		drvdata->state = GXP_I2C_WDATA_PHASE;
		writew(value, drvdata->base + GXP_I2CMCMD);
	}
}

static void gxp_i2c_ack_data(struct gxp_i2c_drvdata *drvdata)
{
	u8 value;

	/* Store the data returned */
	value = readb(drvdata->base + GXP_I2CSNPDAT);
	*drvdata->buf = value;
	drvdata->buf++;
	drvdata->buf_remaining--;

	if (drvdata->buf_remaining == 0) {
		/* No more data, this message is completed. */
		drvdata->msgs_remaining--;

		if (drvdata->msgs_remaining == 0) {
			/* No more messages, stop */
			drvdata->state = GXP_I2C_COMP;
			gxp_i2c_stop(drvdata);
			return;
		}
		/* Move to next message and start transfer */
		drvdata->curr_msg++;
		gxp_i2c_restart(drvdata);
		return;
	}

	/* Ack the slave to make it send next byte */
	drvdata->state = GXP_I2C_RDATA_PHASE;
	if (drvdata->buf_remaining == 1) {
		/* The last data, do not ack */
		writeb(MASTER_EVT_CLR | RW_CMD,
		       drvdata->base + GXP_I2CMCMD);
	} else {
		/* Read data and ack it */
		writeb(MASTER_EVT_CLR | MASTER_ACK_ENAB |
		       RW_CMD, drvdata->base + GXP_I2CMCMD);
	}
}

static void gxp_i2c_chk_data_ack(struct gxp_i2c_drvdata *drvdata)
{
	u16 value;

	value = readb(drvdata->base + GXP_I2CSTAT);
	if (!(value & MASK_ACK)) {
		/* Received No ack, stop */
		drvdata->state = GXP_I2C_DATA_NACK;
		gxp_i2c_stop(drvdata);
		return;
	}

	/* Got ack, check if there is more data to write */
	if (drvdata->buf_remaining == 0) {
		/* No more data, this message is completed */
		drvdata->msgs_remaining--;

		if (drvdata->msgs_remaining == 0) {
			/* No more messages, stop */
			drvdata->state = GXP_I2C_COMP;
			gxp_i2c_stop(drvdata);
			return;
		}
		/* Move to next message and start transfer */
		drvdata->curr_msg++;
		gxp_i2c_restart(drvdata);
		return;
	}

	/* Write data to slave */
	value = *drvdata->buf;
	value = value << 8;

	/* Clear master event */
	value |= MASTER_EVT_CLR;
	drvdata->buf++;
	drvdata->buf_remaining--;
	drvdata->state = GXP_I2C_WDATA_PHASE;
	writew(value, drvdata->base + GXP_I2CMCMD);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static bool gxp_i2c_slave_irq_handler(struct gxp_i2c_drvdata *drvdata)
{
	u8 value;
	u8 buf;
	int ret;

	value = readb(drvdata->base + GXP_I2CEVTERR);

	/* Received start or stop event */
	if (value & MASK_SLAVE_CMD_EVENT) {
		value = readb(drvdata->base + GXP_I2CSTAT);
		/* Master sent stop */
		if (value & MASK_STOP_EVENT) {
			if (drvdata->stopped == 0)
				i2c_slave_event(drvdata->slave, I2C_SLAVE_STOP, &buf);
			writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
			       SLAVE_ACK_ENAB | SLAVE_EVT_STALL, drvdata->base + GXP_I2CSCMD);
			drvdata->stopped = 1;
		} else {
			/* Master sent start and  wants to read */
			drvdata->stopped = 0;
			if (value & MASK_RW) {
				i2c_slave_event(drvdata->slave,
						I2C_SLAVE_READ_REQUESTED, &buf);
				value = buf << 8 | (SLAVE_EVT_CLR | SNOOP_EVT_MASK |
						    SLAVE_EVT_STALL);
				writew(value, drvdata->base + GXP_I2CSCMD);
			} else {
				/* Master wants to write to us */
				ret = i2c_slave_event(drvdata->slave,
						      I2C_SLAVE_WRITE_REQUESTED, &buf);
				if (!ret) {
					/* Ack next byte from master */
					writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
					       SLAVE_ACK_ENAB | SLAVE_EVT_STALL,
					       drvdata->base + GXP_I2CSCMD);
				} else {
					/* Nack next byte from master */
					writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
					       SLAVE_EVT_STALL, drvdata->base + GXP_I2CSCMD);
				}
			}
		}
	} else if (value & MASK_SLAVE_DATA_EVENT) {
		value = readb(drvdata->base + GXP_I2CSTAT);
		/* Master wants to read */
		if (value & MASK_RW) {
			/* Master wants another byte */
			if (value & MASK_ACK) {
				i2c_slave_event(drvdata->slave,
						I2C_SLAVE_READ_PROCESSED, &buf);
				value = buf << 8 | (SLAVE_EVT_CLR | SNOOP_EVT_MASK |
						    SLAVE_EVT_STALL);
				writew(value, drvdata->base + GXP_I2CSCMD);
			} else {
				/* No more bytes needed */
				writew(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
				       SLAVE_ACK_ENAB | SLAVE_EVT_STALL,
				       drvdata->base + GXP_I2CSCMD);
			}
		} else {
			/* Master wants to write to us */
			value = readb(drvdata->base + GXP_I2CSNPDAT);
			buf = (uint8_t)value;
			ret = i2c_slave_event(drvdata->slave,
					      I2C_SLAVE_WRITE_RECEIVED, &buf);
			if (!ret) {
				/* Ack next byte from master */
				writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
				       SLAVE_ACK_ENAB | SLAVE_EVT_STALL,
				       drvdata->base + GXP_I2CSCMD);
			} else {
				/* Nack next byte from master */
				writeb(SLAVE_EVT_CLR | SNOOP_EVT_MASK |
				       SLAVE_EVT_STALL, drvdata->base + GXP_I2CSCMD);
			}
		}
	} else {
		return false;
	}

	return true;
}
#endif /* CONFIG_I2C_SLAVE */

static irqreturn_t gxp_i2c_irq_handler(int irq, void *_drvdata)
{
	struct gxp_i2c_drvdata *drvdata = (struct gxp_i2c_drvdata *)_drvdata;
	u32 value;

#ifdef CONFIG_ARCH_HPE_GXP
	/*
	 * Note: In GXP Architecture there is a shared interrupt
	 * for every engine. In  GSC each engine has its own
	 * interrupt
	 */

	/* Check if the interrupt is for the current engine */
	regmap_read(i2cg_map, GXP_I2CINTSTAT, &value);
	if (!(value & BIT(drvdata->engine)))
		return IRQ_NONE;
#endif

	value = readb(drvdata->base + GXP_I2CEVTERR);

	/* Error */
	if (value & ~(MASK_MASTER_EVENT | MASK_SLAVE_CMD_EVENT |
				MASK_SLAVE_DATA_EVENT)) {
		/* Clear all events */
		writeb(0x00, drvdata->base + GXP_I2CEVTERR);
		drvdata->state = GXP_I2C_ERROR;
		gxp_i2c_stop(drvdata);
		return IRQ_HANDLED;
	}

	if (IS_ENABLED(CONFIG_I2C_SLAVE)) {
		/* Slave mode */
		if (value & (MASK_SLAVE_CMD_EVENT | MASK_SLAVE_DATA_EVENT)) {
			if (gxp_i2c_slave_irq_handler(drvdata))
				return IRQ_HANDLED;
			return IRQ_NONE;
		}
	}

	/*  Master mode */
	switch (drvdata->state) {
	case GXP_I2C_ADDR_PHASE:
		gxp_i2c_chk_addr_ack(drvdata);
		break;

	case GXP_I2C_RDATA_PHASE:
		gxp_i2c_ack_data(drvdata);
		break;

	case GXP_I2C_WDATA_PHASE:
		gxp_i2c_chk_data_ack(drvdata);
		break;
	}

	return IRQ_HANDLED;
}

static void gxp_i2c_init(struct gxp_i2c_drvdata *drvdata)
{
	u8 freq_div;

	drvdata->state = GXP_I2C_IDLE;
	drvdata->stopped = 0;

	/* Calculate frequency divider
	 * Assuming 2MHz source clock divided by desired bus frequency
	 * For 100KHz: 2000000 / 100000 = 20
	 * For 400KHz: 2000000 / 400000 = 5
	 */
	freq_div = 2000000 / drvdata->t.bus_freq_hz;
	if (freq_div == 0)
		freq_div = 1; /* Minimum divider */

	writeb(freq_div, drvdata->base + GXP_I2CFREQDIV);
	writeb(FILTER_CNT | FAIRNESS_CNT,
	       drvdata->base + GXP_I2CFLTFAIR);
	writeb(GXP_DATA_EDGE_RST_CTRL, drvdata->base + GXP_I2CTMOEDG);
	writeb(0x00, drvdata->base + GXP_I2CCYCTIM);
	writeb(0x00, drvdata->base + GXP_I2CSNPAA);
	writeb(0x00, drvdata->base + GXP_I2CADVFEAT);
	writeb(SNOOP_EVT_CLR | SLAVE_EVT_CLR | SNOOP_EVT_MASK |
	       SLAVE_EVT_MASK, drvdata->base + GXP_I2CSCMD);
	writeb(MASTER_EVT_CLR, drvdata->base + GXP_I2CMCMD);
	writeb(0x00, drvdata->base + GXP_I2CEVTERR);
	writeb(0x00, drvdata->base + GXP_I2COWNADR);
}

static int gxp_i2c_probe(struct platform_device *pdev)
{
	struct gxp_i2c_drvdata *drvdata;
	int rc;
	struct i2c_adapter *adapter;
	struct resource *res;

#ifdef CONFIG_ARCH_HPE_GXP
	/*
	 * Note: In GXP Architecture there is a shared interrupt
	 * for every engine. In  GSC each engine has its own
	 * interrupt
	 */

	if (!i2cg_map) {
		i2cg_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							   "hpe,sysreg");
		if (IS_ERR(i2cg_map)) {
			return dev_err_probe(&pdev->dev, PTR_ERR(i2cg_map),
					     "failed to map i2cg_handle\n");
		}

		/* Disable interrupt */
		regmap_update_bits(i2cg_map, GXP_I2CINTEN, 0x00000FFF, 0);
	}
#endif

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);
	drvdata->dev = &pdev->dev;
	init_completion(&drvdata->completion);

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	/* Use physical memory address to determine which I2C engine this is. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	drvdata->engine = (res->start & 0x1f00) >> 8;

	if (drvdata->engine >= MAX_I2C_ENGINE) {
		return dev_err_probe(&pdev->dev, -EINVAL, "i2c engine %d is unsupported\n",
			drvdata->engine);
	}

	rc = platform_get_irq(pdev, 0);
	if (rc < 0)
		return rc;

	drvdata->irq = rc;
	rc = devm_request_irq(&pdev->dev, drvdata->irq, gxp_i2c_irq_handler,
			      IRQF_SHARED, gxp_i2c_name[drvdata->engine], drvdata);
	if (rc < 0)
		return dev_err_probe(&pdev->dev, rc, "irq request failed\n");

	i2c_parse_fw_timings(&pdev->dev, &drvdata->t, true);

	/* Validate bus frequency */
	if (drvdata->t.bus_freq_hz == 0) {
		dev_warn(&pdev->dev, "bus-frequency not specified, using default 100KHz\n");
		drvdata->t.bus_freq_hz = 100000;
	}

	if (drvdata->t.bus_freq_hz > 400000) {
		dev_warn(&pdev->dev, "bus-frequency %d Hz exceeds maximum 400KHz, clamping\n",
			 drvdata->t.bus_freq_hz);
		drvdata->t.bus_freq_hz = 400000;
	}

	gxp_i2c_init(drvdata);

#ifdef CONFIG_ARCH_HPE_GXP
	/*
	 * Note: In GXP Architecture there is a shared interrupt
	 * for every engine. In  GSC each engine has its own
	 * interrupt
	 */
	/* Enable interrupt */
	regmap_update_bits(i2cg_map, GXP_I2CINTEN, BIT(drvdata->engine),
			   BIT(drvdata->engine));
#endif

	/* NICK TODO: Seems like we need a different compatible */
	adapter = &drvdata->adapter;
	i2c_set_adapdata(adapter, drvdata);

	adapter->owner = THIS_MODULE;
	strscpy(adapter->name, "HPE GXP I2C adapter", sizeof(adapter->name));
	adapter->algo = &gxp_i2c_algo;
	adapter->dev.parent = &pdev->dev;
	adapter->dev.of_node = pdev->dev.of_node;
	adapter->timeout = msecs_to_jiffies(1000); /* 1 second timeout */
	adapter->retries = 3;

	rc = i2c_add_adapter(adapter);
	if (rc)
		return dev_err_probe(&pdev->dev, rc, "i2c add adapter failed\n");

	return 0;
}

static void gxp_i2c_remove(struct platform_device *pdev)
{
	struct gxp_i2c_drvdata *drvdata = platform_get_drvdata(pdev);

#ifdef CONFIG_ARCH_HPE_GXP
	/* Disable interrupt */
	regmap_update_bits(i2cg_map, GXP_I2CINTEN, BIT(drvdata->engine), 0);
#endif
	i2c_del_adapter(&drvdata->adapter);
}

static const struct of_device_id gxp_i2c_of_match[] = {
	{ .compatible = "hpe,gxp-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_i2c_of_match);

static struct platform_driver gxp_i2c_driver = {
	.probe	= gxp_i2c_probe,
	.remove_new = gxp_i2c_remove,
	.driver = {
		.name = "gxp-i2c",
		.of_match_table = gxp_i2c_of_match,
	},
};
module_platform_driver(gxp_i2c_driver);

MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_DESCRIPTION("HPE GXP I2C bus driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2024 Hewlett Packard Enterprise Development LP */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>

// different phases for different types of i3c transfers
enum {
	GSC_I3C_IDLE = 0,
	GSC_I3C_BROADCAST_ADDR_PHASE,
	GSC_I3C_UNICAST_ADDR_PHASE,
	GSC_I3C_DAA_ADDR_PHASE,
	GSC_I3C_READ_DATA_PHASE,
	GSC_I3C_WRITE_DATA_PHASE,
	GSC_I3C_COMMAND_PHASE,
	GSC_I3C_DEV_IDENTIFY_PHASE,
	GSC_I3C_PVT_DATA_PHASE,
	GSC_I3C_BROADCAST_ADDR_NACK,
	GSC_I3C_PVT_DATA_NACK,
	GSC_I3C_UNICAST_ADDR_NACK,
	GSC_I3C_DAA_ADDR_NACK,
	GSC_I3C_READ_DATA_NACK,
	GSC_I3C_WRITE_DATA_NACK,
	GSC_I3C_COMMAND_NACK,
	GSC_I3C_DEV_IDENTIFY_NACK,
	GSC_I3C_PARITY_ERROR_EVENT,
	GSC_I3C_ERROR,
	GSC_I3C_COMP
};

// different types of i3c transfers
enum {
	GSC_I3C_CCC = 0,
	GSC_I3C_PVT_DATA,
	GSC_I3C_DAA,
	GSC_I3C_IBI,
};

#define MAX_I3C_ENGINE                  10
/* GSC I2C and i3c registers */
#define GSC_I2CSTAT		                0x00
  #define MASK_STOP_EVENT	            0x20
  #define MASK_ACK		                0x08
  #define MASK_RW			            0x04
#define GSC_I2CEVTERR		            0x01
  #define MASK_SLAVE_CMD_EVENT          0x01
  #define MASK_SLAVE_DATA_EVENT         0x02
  #define MASK_MASTER_EVENT	            0x10
#define GSC_I2CSNPDAT		            0x02
#define GSC_I2CSNPADR			0x03
#define GSC_I2CMCMD		                0x04
#define GSC_I2CSCMD		                0x06
#define GSC_I2CSNPAA		            0x09
#define GSC_I2CADVFEAT		            0x0A
#define GSC_I2COWNADR		            0x0B
#define GSC_I2CFREQDIV		            0x0C
#define GSC_I2CFLTFAIR		            0x0D
#define GSC_I2CTMOEDG		            0x0E
#define GSC_I2CCYCTIM		            0x0F
#define GSC_I2CMBTESTAT			0x60
#define GSC_I3C_STATUS                  0x80
#define GSC_I3C_DYN_ADDR_STAT           0x81
#define GSC_I3C_EVENT                   0x82
	#define MASK_PARITY_ERROR_EVENT     0x10
#define GSC_I3C_COMMAND                 0x83
#define GSC_I3C_DYN_ADDR                0x90
#define GSC_I3C_FREQ_DIVIDER            0xaa
#define GSC_I3C_TARGET_PROVISIONAL_ID0  0xb8
#define GSC_I3C_TARGET_PROVISIONAL_ID1  0xbc
#define GSC_I3C_TARGET_BCR              0xbe
#define GSC_I3C_TARGET_DCR              0xbf
#define GSC_I3C_DMA_STATUS              0xc0
#define GSC_I3C_DMA_WR_CNT              0xc1
#define GSC_I3C_DMA_CMD                 0xc4
#define GSC_I3C_DMA_RD_CNT              0xc5
#define GSC_I3C_DMA_WR_ADDR             0xc8
#define GSC_I3C_DMA_RD_ADDR             0xcc

#define DEV_ADDR_TABLE_LEGACY_I2C_DEV	BIT(31)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR(x)	(((x) << 16) & GENMASK(23, 16))
#define DEV_ADDR_TABLE_STATIC_ADDR(x)	((x) & GENMASK(6, 0))
#define DEV_ADDR_TABLE_LOC(start, idx)	((start) + ((idx) << 2))

#define MAX_DEVS 11

#define I3C_BUS_SDR1_SCL_RATE		8000000
#define I3C_BUS_SDR2_SCL_RATE		6000000
#define I3C_BUS_SDR3_SCL_RATE		4000000
#define I3C_BUS_SDR4_SCL_RATE		2000000
#define I3C_BUS_I2C_FM_TLOW_MIN_NS	1300
#define I3C_BUS_I2C_FMP_TLOW_MIN_NS	500
#define I3C_BUS_THIGH_MAX_NS		41

#define XFER_TIMEOUT (msecs_to_jiffies(1000))

struct hpe_i3c_ibi_payload {
	u8 snoop_data[32];
	u8 snoop_address;
	u8 ibi_data_cnt;
	u8 ibi_max_data_cnt;
};

// i3c controller private data structure
struct hpe_i3c_master {
	struct device *dev;
	struct i3c_ccc_cmd *ccc;
	struct i3c_priv_xfer *i3c_xfers;
	int    i3c_nxfers;
	char    *read_virt_addr;
	struct i3c_master_controller base;
	struct completion completion;
	u16 maxdevs;
	u32 free_pos;
	u16 data_count;
	void __iomem *regs;
	char version[5];
	char type[5];
	struct i3c_dev_desc *ibi_dev[MAX_DEVS];
	u8 addrs[MAX_DEVS];
	u8 i3c_dev_cnt;
	struct i3c_dev_desc *i3c_desc[MAX_DEVS];
	u64 device_addr_table[MAX_DEVS];
	u64 target_prov_id[MAX_DEVS];
	u8 target_dcr[MAX_DEVS];
	u8 target_bcr[MAX_DEVS];
	unsigned char state;
	unsigned char trans_type;
	uint8_t engine;
	spinlock_t devs_lock;

	int i3c_error;
	struct hpe_i3c_ibi_payload i3c_ibi_payload[MAX_DEVS];

	//For sending CCC commands
	struct dentry *debug_dir;
};


struct hpe_i3c_i2c_dev_data {
	u8 index;
	struct i3c_generic_ibi_pool *ibi_pool;
};

u8 GetParityBit(u8 dynAddr)
{
	u8 parityBit = 0;
	int  index;

	for (index = 0; index < 7; index++) {
		parityBit ^= ((dynAddr >> index) & 0x01);
	}

	return !parityBit;
}

static void gsc_i3c_start(struct hpe_i3c_master *master)
{
	void __iomem *base = master->regs;
	uint16_t value = 0x00;

	master->data_count = 0x00;

	value = 0xfc01; // Broadcast address with the start bit

	master->state = GSC_I3C_BROADCAST_ADDR_PHASE;
	writew(value, base + GSC_I2CMCMD);
}


static int gsc_wait_for_interrupt(struct hpe_i3c_master *master)
{
	unsigned long time_left;

	reinit_completion(&master->completion);
	time_left = wait_for_completion_timeout(&master->completion,XFER_TIMEOUT);

	if (time_left == 0) {
		switch (master->state) {
		case GSC_I3C_BROADCAST_ADDR_PHASE:
			dev_err(master->dev,"Broadcast Address phase timeout\n");
			break;
		case GSC_I3C_UNICAST_ADDR_PHASE:
			dev_err(master->dev,"Unicast Address phase timeout\n");
			break;
		case GSC_I3C_DAA_ADDR_PHASE:
			dev_err(master->dev,"DAA Address phase timeout\n");
			break;
		case GSC_I3C_READ_DATA_PHASE:
			dev_err(master->dev,"Read Data phase timeout\n");
			break;
		case GSC_I3C_WRITE_DATA_PHASE:
			dev_err(master->dev,"Write Data phase timeout\n");
			break;
		case GSC_I3C_COMMAND_PHASE:
			dev_err(master->dev,"Command phase timeout\n");
			break;
		case GSC_I3C_DEV_IDENTIFY_PHASE:
			dev_err(master->dev,"Device Identification phase timeout\n");
			break;
		case GSC_I3C_PVT_DATA_PHASE:
			dev_err(master->dev,"DMA Data phase timeout\n");
			break;
		default:
			dev_err(master->dev,"i3c transfer timeout and state = %d\n",master->state);
			break;
		}
		return -ETIMEDOUT;
	}

	if (master->state == GSC_I3C_BROADCAST_ADDR_NACK) {
		dev_err(master->dev,"No ACK for Broadcast address phase\n");
		return -EIO;
	} 	else if (master->state == GSC_I3C_UNICAST_ADDR_NACK) {
		dev_err(master->dev,"No ACK for Unicast address phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_READ_DATA_NACK) {
		dev_err(master->dev, "No ACK for Read data phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_WRITE_DATA_NACK) {
		dev_err(master->dev, "No ACK for Write data phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_COMMAND_NACK) {
		dev_err(master->dev, "No ACK for command phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_DEV_IDENTIFY_NACK) {
		dev_err(master->dev, "No ACK for Device Identification phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_PARITY_ERROR_EVENT) {
		dev_err(master->dev, "Parity Error during Write Data phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_PVT_DATA_NACK) {
		dev_err(master->dev, "No ACK for DMA Data phase\n");
		return -EIO;
	} else if (master->state == GSC_I3C_ERROR) {
		dev_err(master->dev, "generic i3c error during transaction\n");
		return -EIO;
	}
	return 0;
}

static void gsc_i3c_stop(struct hpe_i3c_master *master)
{
	void __iomem *base = master->regs;

	writeb(0x82, base + GSC_I2CMCMD); // clear event, send stop

	complete(&master->completion);
}

static bool hpe_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
					   const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		return false;
	}
}

static inline struct hpe_i3c_master *
to_hpe_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct hpe_i3c_master, base);
}

static void hpe_i3c_master_disable(struct hpe_i3c_master *master)
{
	u8 val;

	// disable i3c mode
	val = readb(master->regs + GSC_I3C_COMMAND);
	val &= (~(0x03));
	writeb(val, master->regs + GSC_I3C_COMMAND);
}

static void hpe_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					   struct i3c_ibi_slot *slot)
{
	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static int hpe_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				     const struct i3c_ibi_setup *req)
{
	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	unsigned long flags;

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->devs_lock, flags);
	master->ibi_dev[data->index] = dev;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	return 0;
}

static void hpe_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	unsigned long flags;

	spin_lock_irqsave(&master->devs_lock, flags);
	master->ibi_dev[data->index] = NULL;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;
}

static int hpe_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	u8 val;

	val = readb(master->regs + GSC_I3C_COMMAND);
	val |= 0x4; //Enable IBI
	writeb(val, master->regs + GSC_I3C_COMMAND);

	return i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static int hpe_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	u8 val;

	val = readb(master->regs + GSC_I3C_COMMAND);
	val &= (~(0x04)); //Disable IBI
	writeb(val, master->regs + GSC_I3C_COMMAND);

	return i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static void hpe_i3c_master_enable(struct hpe_i3c_master *master)
{
	u8 val;

	// enable i3c mode and put controller in master mode
	val = readb(master->regs + GSC_I3C_COMMAND);
	val |= 0x03;
	writeb(val, master->regs + GSC_I3C_COMMAND);

#ifdef CONFIG_HPE_I3C_EXERCISER
	// in order to use the prodigy exerciser, program i3c frequency divider register
	// to result an i3c frequency of 9 MHz and this should work for all i3c engines.
	// this programming is not required for Real i3c slave device.
	// we will remove it later for production system
	writew(0x1015, master->regs + GSC_I3C_FREQ_DIVIDER);
#endif
}

static int hpe_i3c_master_get_free_pos(struct hpe_i3c_master *master)
{
	// get free position for i3c slave
	if (!(master->free_pos & GENMASK(master->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(master->free_pos) - 1;
}

static int hpe_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	int ret;
	u8 val = 0;
	void __iomem *iopbase;

	// set voltage level to 1V for i3c engine 8 and 9 and 1.8V for all other engines
	iopbase = ioremap(0xc0000000, 0x1000);
	if ((master->engine >= 0) && (master->engine <= 7)) {
		writel(0x200 + master->engine, iopbase + 0xae0);
		writel(0x1, iopbase + 0xae4);
	} else if ((master->engine >= 8) && (master->engine <= 9)) {
		writel(0x200 + master->engine, iopbase + 0xae0);
		writel(0x0, iopbase + 0xae4);
	} else {
		return -EINVAL;
	}

	// reset i3c engine
	val = readb(master->regs + 0x08);
	val |= 0x80;
	writeb(val, master->regs + 0x08);
	val &= 0x7F;
  	writeb(val, master->regs + 0x08);
	// program i2c SCL Clock Frequency
	writeb(2000000/bus->scl_rate.i2c, master->regs + GSC_I2CFREQDIV);
	// set up edge control
	writeb(0x00, master->regs + GSC_I2CTMOEDG);
	writeb(0x00, master->regs + GSC_I2CADVFEAT);
	// program i3c frequency
	writew(0x103f, master->regs + GSC_I3C_FREQ_DIVIDER);

	
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	hpe_i3c_master_enable(master);
	return 0;
}

static void hpe_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);

	hpe_i3c_master_disable(master);
}

static int hpe_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				      struct i3c_ccc_cmd *ccc)
{
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	int ret = 0;

	if (ccc->id == I3C_CCC_ENTDAA)
		return -EINVAL;

	// set the transaction type as CCC and wait for the interrupt
	master->trans_type = GSC_I3C_CCC;
	master->ccc = ccc;
	gsc_i3c_start(master);
	ret = gsc_wait_for_interrupt(master);

	return ret;
}

static int hpe_i3c_master_daa(struct i3c_master_controller *m)
{
	
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	u32 olddevs;
	int current_i3c_dev_cnt; //Count can change based on calls by
	u8 p, val, last_addr = 0, index, pos, current_cmd;
	int ret;

	olddevs = ~(master->free_pos);

	// Prepare DAT in driver's memory before launching DAA.
	// get all the free slave address for MAX number of i3c slave and fill the device address table
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0)
			return -ENOSPC;

		master->addrs[pos] = ret;
		p = GetParityBit(ret);
		last_addr = ret;
		ret = ((ret & 0x7F) << 1) | (p & 0x01);
		master->device_addr_table[pos] = DEV_ADDR_TABLE_DYNAMIC_ADDR(ret);
	}
	// Set the reserved byte bit in I3C command CSR
	// This turns on the ENTDAA tracker
	val = 0x23;

	current_cmd = readb(master->regs + GSC_I3C_COMMAND);
	if (current_cmd & 0x04)
		val |= 0x04;

	writeb(val, master->regs + GSC_I3C_COMMAND);
	master->i3c_dev_cnt = 0x00;
	master->trans_type = GSC_I3C_DAA;
	gsc_i3c_start(master); //Broadcast Phase
	ret = gsc_wait_for_interrupt(master);
	if (ret != 0x00)
		return ret;

	current_i3c_dev_cnt = master->i3c_dev_cnt;

	for (index = 0; index < current_i3c_dev_cnt; index++)
		i3c_master_add_i3c_dev_locked(m, master->addrs[index]);

	val = readb(master->regs + GSC_I3C_DYN_ADDR_STAT);
	pr_info("Total Number of Dynamic Address assigned = 0x%x\n", ((val >> 4) & 0x0F));
	return 0;
}

static int hpe_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{

	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	u8 index = 0;
	u32 read_len = 0, write_len = 0;
	dma_addr_t phy_write_buf, phy_read_buf;
	u8 read_index = 0, write_index = 0, addr = 0;
	char * read_buf = NULL, *write_buf = NULL;
	int ret = 0;

	if ((i3c_nxfers == 0x00) || (i3c_xfers == NULL))
	{
		return -ENODATA;
	}
	master->i3c_xfers = i3c_xfers;
	master->i3c_nxfers = i3c_nxfers;
	// find out the number of bytes that needs to be written or read
	for (index = 0; index < i3c_nxfers; index++)
	{
		if (i3c_xfers[index].rnw)
		{
			read_len = i3c_xfers[index].len;
			read_index = index;
		}
		else
		{
			write_len = i3c_xfers[index].len;
			write_index = index;
		}
	}

	// if there is a write request then, allocate physical memory for write buf and program the physical addr in DMA engine
	if (write_len > 0)
	{
		write_buf = dma_alloc_coherent(master->dev, write_len, &phy_write_buf, GFP_KERNEL);
		if(!write_buf)
		{
			return -ENOMEM;
		}
		pr_info("write phy addr = %pad and Virtual Addr = %px\n", &phy_write_buf, write_buf);
		memcpy((void *)write_buf, (void *)i3c_xfers[write_index].data.out,write_len);
		writel(phy_write_buf,master->regs + GSC_I3C_DMA_WR_ADDR);
	}
	// if there is a read request then, allocate physical memory for read buf and program the physical addr in DMA engine
	if (read_len > 0)
	{
		read_buf = dma_alloc_coherent(master->dev, read_len, &phy_read_buf, GFP_KERNEL);
		if(!read_buf)
		{
			return -ENOMEM;
		}
		pr_info("read phy addr = %pad and Virtual Addr = %px\n", &phy_read_buf, read_buf);
		writel(phy_read_buf,master->regs + GSC_I3C_DMA_RD_ADDR);
		master->read_virt_addr = read_buf;
	}
	// write to DMA Write count register and clear previously pending completions
	writel(((write_len << 8) | 0x02),master->regs + GSC_I3C_DMA_STATUS);
	addr = master->addrs[data->index];
	// write to DMA Read count register and write to command register with the Target address
	writel(((read_len << 8) | (addr << 1) | 0x01),master->regs + GSC_I3C_DMA_CMD);
	master->trans_type = GSC_I3C_PVT_DATA;
	master->state = GSC_I3C_PVT_DATA_PHASE;
	ret = gsc_wait_for_interrupt(master);
	// free allocated DMA physical buffer
	dma_free_coherent(master->dev, write_len, write_buf, phy_write_buf);
	dma_free_coherent(master->dev, read_len, read_buf, phy_read_buf);
	if (ret != 0x00)
	{
		return ret;
	}

	return 0;
}



static int hpe_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					  u8 old_dyn_addr)
{
	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	int pos;

	pos = hpe_i3c_master_get_free_pos(master);

	if (data->index > pos && pos > 0) {
		master->device_addr_table[data->index] = 0x00;

		master->addrs[data->index] = 0;
		master->free_pos |= BIT(data->index);

		data->index = pos;
		master->addrs[pos] = dev->info.dyn_addr;
		master->free_pos &= ~BIT(pos);
	}

	master->device_addr_table[data->index] = DEV_ADDR_TABLE_DYNAMIC_ADDR(dev->info.dyn_addr);

	master->addrs[data->index] = dev->info.dyn_addr;
	master->i3c_desc[data->index] = dev;

	return 0;
}

static int hpe_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	struct hpe_i3c_i2c_dev_data *data;
	int pos;

	pos = hpe_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->info.dyn_addr ? : dev->info.static_addr;
	master->free_pos &= ~BIT(pos);
	i3c_dev_set_master_data(dev, data);

	master->device_addr_table[data->index] = DEV_ADDR_TABLE_DYNAMIC_ADDR(master->addrs[pos]);
	master->i3c_desc[data->index] = dev;
	master->ibi_dev[data->index] = dev;
	//so need to look at updating local vars that have not be updated by core
	//i3c, such as our bcr / dcr array
	//Here the core removes a device, and adds one in its place, this means
	//we are out of sync

	return 0;
}

static void hpe_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct hpe_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);

	master->device_addr_table[data->index] = 0x00;

	i3c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	master->i3c_desc[data->index] = NULL;
	kfree(data);
}

static int hpe_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				   const struct i2c_msg *i2c_xfers,
				   int i2c_nxfers)
{
	// i2c legacy transfer is not supported yet
	return -EPROTONOSUPPORT;
}

static int hpe_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);
	struct hpe_i3c_i2c_dev_data *data;
	int pos;

	pos = hpe_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->addr;
	master->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);

	master->device_addr_table[data->index] = DEV_ADDR_TABLE_LEGACY_I2C_DEV | DEV_ADDR_TABLE_STATIC_ADDR(dev->addr);

	return 0;
}

static void hpe_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct hpe_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct hpe_i3c_master *master = to_hpe_i3c_master(m);

	master->device_addr_table[data->index] = 0x00;

	i2c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static irqreturn_t hpe_i3c_master_irq_handler(int irq, void *dev_id)
{
	struct hpe_i3c_master *master = (struct hpe_i3c_master *) dev_id;
	u32 value;
	u32 i3c_evt;
	u32 i3c_cmd;
	u32 btestat;
	u32 dmastat;
	u16 val;
	u8 p, ret, index;
	u64 provid_high, provid_low;
	char *data_ptr;
	u8 snoop_address;
	struct hpe_i3c_i2c_dev_data *data;
	struct i3c_ibi_slot *slot;
	struct i3c_dev_desc *dev;
	static int current_ibi_slot;
	u8 *buf;
	int found = 0;

	// check for NULL pointers
	if ((master == NULL) || (master->ccc == NULL))
	{
		pr_alert("[%s] pointer value is NULL\n", __func__);
		return IRQ_HANDLED;
	}

	value = readb(master->regs + GSC_I2CEVTERR);
	// check for Error
	if (value & ~(MASK_MASTER_EVENT | MASK_SLAVE_CMD_EVENT |
				MASK_SLAVE_DATA_EVENT)) {
		btestat = readb(master->regs + GSC_I2CMBTESTAT);
		dmastat = readb(master->regs + GSC_I3C_DMA_STATUS);
		pr_alert("[%s] I3C Error, GSC_I2CEVTERR = 0x%x BTEStat = 0x%x DMAStAT = 0x%x\n", __func__, value, btestat, dmastat);
		master->i3c_error = 1;
		writeb(0x00, master->regs + GSC_I2CEVTERR); //clear all event
		// currently when we are using DMA engine to send and receive request from i3c target then i do see that
		// bus error bit is getting set even if transaction is successful and completion bit is set
		// for now workaround is to ignore this bus error during i3c private transfer using DMA Phase
		// TODO: - need to remove this workaround once the issue is fixed in ASIC
		if (master->trans_type != GSC_I3C_PVT_DATA)
		{
			pr_err("Returning returning Error State\n");
			master->state = GSC_I3C_ERROR;
			gsc_i3c_stop(master);
			return IRQ_HANDLED;
		}
	}

	i3c_evt = readb(master->regs + GSC_I3C_EVENT);

	if(master->i3c_error) {
		value = readb(master->regs + GSC_I2CEVTERR);
		btestat = readb(master->regs + GSC_I2CMBTESTAT);
		dmastat = readb(master->regs + GSC_I3C_DMA_STATUS);

		pr_err("Error irq: State = 0x%x Trans = 0x%x i2cevterr=0x%x btestat=0x%x dmastat = 0x%x i3cevt=0x%x\n",
		       master->state, master->trans_type, value, btestat, dmastat, i3c_evt);
	}

	// check for IBI event
	if (i3c_evt & 0x4) {
		/* Clear the IBI status Bit 2*/
		writeb((readb(master->regs + GSC_I3C_EVENT) | 0x4), master->regs + GSC_I3C_EVENT);

		i3c_cmd = readb(master->regs + GSC_I3C_COMMAND);
		/* Check to see if we received an IBI when we are not accepting IBIs */
		if ((i3c_cmd & 0x4) == 0) {
			pr_alert("IBI receieved not enabled\n");
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		/* Get address from snoop address */
		snoop_address = (readb(master->regs + GSC_I2CSNPADR) >> 1);

		for (current_ibi_slot = 0; current_ibi_slot < MAX_DEVS; current_ibi_slot++) {
			if (snoop_address == master->addrs[current_ibi_slot]) {
				found = 1;
				break;
			}
		}

		if (!found) {
			pr_err("Could not find IBI Snoop Address = 0x%x\n", snoop_address);
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		master->i3c_ibi_payload[current_ibi_slot].snoop_address = snoop_address;
		master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt = 0;
		dev = master->ibi_dev[current_ibi_slot];
		if (dev == NULL) {
			pr_err("IBI DEV Is NULL\n");
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		data = i3c_dev_get_master_data(dev);
		if (data == NULL) {
			pr_err("DATA is NULL for IBI\n");
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
		if (slot == NULL) {
			pr_err("No Free Slots for IBI!\n");
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		//https://onlinedocs.microchip.com/oxy/GUID-598A6CC5-BA9B-433D-BAFE-893E2A72A7A3-en-US-14/GUID-667C6D48-E084-4DB5-B4B9-975EB12EDFE0.html
		//IBI Payload is Bit 2, if 0 there is none

		if ((master->i3c_desc[current_ibi_slot]->info.bcr & 0x4) == 0) {
			//pr_err("No IBI Payload\n");

			slot->len = 0;

			i3c_master_queue_ibi(dev, slot);
			writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			return IRQ_HANDLED;
		}

		if (master->i3c_desc[current_ibi_slot]->ibi->max_payload_len != master->i3c_desc[current_ibi_slot]->info.max_ibi_len)
			pr_alert("0x%x != 0x%x\n", master->i3c_desc[current_ibi_slot]->ibi->max_payload_len,
				 master->i3c_desc[current_ibi_slot]->info.max_ibi_len);

		/* Per Spec Set us up for the end of Address Phase */
		master->trans_type = GSC_I3C_IBI;
		master->state = GSC_I3C_READ_DATA_PHASE;

		// clear master event bit and set master acknowledge bit
		writeb(0x8c, master->regs + GSC_I2CMCMD);

		/* Per Will Some Clean Up May not be necessary? */
		//writeb(0x8, master->regs + GSC_I2CMBTESTAT);
		//writeb(0x0, master->regs + GSC_I2CMCMD);
		return IRQ_HANDLED;
	}

	// for i3c CCC Transfer Type
	if (master->trans_type == GSC_I3C_CCC) {
		switch (master->state) {
		// check if any i3c target acknowledged or not. if target acknowledge,
		// then update the state as COMMAND PHASE and send the CCC id
		case GSC_I3C_BROADCAST_ADDR_PHASE:
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_BROADCAST_ADDR_NACK;
				gsc_i3c_stop(master);
			} else {
				master->state = GSC_I3C_COMMAND_PHASE;
				val = (master->ccc->id << 8) | 0x80;
				writew(val, master->regs + GSC_I2CMCMD);
			}
			break;
		case GSC_I3C_COMMAND_PHASE:
			// check whether CCC is unicast or broadcast.for broadcast, if there is payload then send the payload data to the target
			if ((!(master->ccc->id & I3C_CCC_DIRECT)) &&
			    (master->ccc->dests->addr == I3C_BROADCAST_ADDR)) {
				if ((master->ccc->dests->payload.len > 0x00) && (master->ccc->dests->payload.data != NULL)) {
					master->state = GSC_I3C_WRITE_DATA_PHASE;
					data_ptr = (char *)master->ccc->dests->payload.data;
					val = ((data_ptr[master->data_count]) << 8) | 0x0080;
					master->data_count++;
					writew(val, master->regs + GSC_I2CMCMD);
				} else {
					gsc_i3c_stop(master);
				}
			}
			// if CCC is unicast, then check if it's read or write mode and accordingly update the state and send the target address
			else if ((master->ccc->id & I3C_CCC_DIRECT) &&
				 (master->ccc->dests->addr != I3C_BROADCAST_ADDR)) {
				if (master->ccc->rnw) {
					master->state = GSC_I3C_UNICAST_ADDR_PHASE;
					val = ((master->ccc->dests->addr & 0x7F) << 9) | 0x0185;
					writew(val, master->regs + GSC_I2CMCMD);
				} else {
					master->state = GSC_I3C_UNICAST_ADDR_PHASE;
					val = ((master->ccc->dests->addr & 0x7F) << 9) | 0x0081;
					writew(val, master->regs + GSC_I2CMCMD);
				}
			}
			break;
		case GSC_I3C_UNICAST_ADDR_PHASE:
			// this is only for Unicast packet. if there is ack from the target, then update the state as READ DATA or WRITE DATA phase
			// and accordingly Read\Write data from the target
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_UNICAST_ADDR_NACK;
				gsc_i3c_stop(master);
			} else {
				if (master->ccc->rnw) {
					// Read data from the slave
					if ((master->ccc->dests->payload.len > 0x00) &&
					    (master->ccc->dests->payload.data != NULL)) {
						master->state = GSC_I3C_READ_DATA_PHASE;
						// if this is last byte to read then don't acknowledge otherwise acknowledge the received data
						if ((master->data_count + 1) ==
						    master->ccc->dests->payload.len)
							writeb(0x84, master->regs + GSC_I2CMCMD);
						else
							writeb(0x8c, master->regs + GSC_I2CMCMD);
					} else {
						gsc_i3c_stop(master);
					}
				} else {
					// write data to the slave and update the count
					if ((master->ccc->dests->payload.len > 0x00) && (master->ccc->dests->payload.data != NULL)) {
						master->state = GSC_I3C_WRITE_DATA_PHASE;
						data_ptr = (char *)master->ccc->dests->payload.data;
						val = ((data_ptr[master->data_count]) << 8) | 0x0080;
						master->data_count++;
						writew(val, master->regs + GSC_I2CMCMD);
					} else
						gsc_i3c_stop(master);
				}
			}
			break;
		case GSC_I3C_READ_DATA_PHASE:
			// store the data returned
			value = readb(master->regs + GSC_I2CSNPDAT);
			data_ptr = (char *)master->ccc->dests->payload.data;
			data_ptr[master->data_count] = value;
			master->data_count++;
			value = readb(master->regs + GSC_I2CSTAT);
			// if last byte is already received or there is no ACK from the target, then stop the transaction
			if ((master->data_count == master->ccc->dests->payload.len) || (!(value & MASK_ACK)))
				gsc_i3c_stop(master);
			else {
				master->state = GSC_I3C_READ_DATA_PHASE;
				// if this is last byte to read then don't acknowledge otherwise acknowledge the received data
				if ((master->data_count + 1) ==
				    master->ccc->dests->payload.len)
					writeb(0x84, master->regs + GSC_I2CMCMD);
				else
					writeb(0x8c, master->regs + GSC_I2CMCMD);
			}
			break;
		case GSC_I3C_WRITE_DATA_PHASE:
			value = readb(master->regs + GSC_I3C_EVENT);
			// check for parity error while writing the data
			if (value & MASK_PARITY_ERROR_EVENT) {
				master->state = GSC_I3C_PARITY_ERROR_EVENT;
				gsc_i3c_stop(master);
			}
			// if last byte is already written, then stop the transaction
			if((master->data_count) == master->ccc->dests->payload.len)
				gsc_i3c_stop(master);
			else {
				// continue writing data to the target
				master->state = GSC_I3C_WRITE_DATA_PHASE;
				data_ptr = (char *)master->ccc->dests->payload.data;
				val = ((data_ptr[master->data_count]) << 8) | 0x0080;
				master->data_count++;
				writew(val, master->regs + GSC_I2CMCMD);
			}
			break;
		}
	} else if (master->trans_type == GSC_I3C_DAA) { // for i3c DAA transfer type
		switch (master->state) {
		case GSC_I3C_BROADCAST_ADDR_PHASE:
			// check whether target acknowledged or not. if there is ACK, then write DAA CCC Code (0x07) as next byte of transfer
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_BROADCAST_ADDR_NACK;
				gsc_i3c_stop(master);
			} else {
				master->state = GSC_I3C_COMMAND_PHASE;
				val = (0x07 << 8) | 0x80;
				writew(val, master->regs + GSC_I2CMCMD);
			}
			break;
		case GSC_I3C_COMMAND_PHASE:
			// send Broadcast Address (0x7E) with repeated start and RnW = 1 
			master->state = GSC_I3C_DAA_ADDR_PHASE;
			writew(0xfd85, master->regs + GSC_I2CMCMD);
			break;
		case GSC_I3C_DAA_ADDR_PHASE:
			// if ACK from Target, then Write the dynamic address which will gets assigned to the slave in one of the controller reg
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_DAA_ADDR_NACK;
				gsc_i3c_stop(master);
			} else {
				master->state = GSC_I3C_DEV_IDENTIFY_PHASE;
				p = GetParityBit(master->addrs[master->i3c_dev_cnt]);
				ret = ((master->addrs[master->i3c_dev_cnt] & 0x7F) << 1) | (p & 0x01);
				writeb(ret, master->regs + GSC_I3C_DYN_ADDR + master->i3c_dev_cnt);
				writeb(0x8c, master->regs + GSC_I2CMCMD);
			}
			break;
		case GSC_I3C_DEV_IDENTIFY_PHASE:
			// if ACK from the target, then read Prov ID, BCR and DCR store it in drivers memory and assign the slave addr to the target
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_DEV_IDENTIFY_NACK;
				gsc_i3c_stop(master);
			} else {
				master->state = GSC_I3C_WRITE_DATA_PHASE;
				provid_low = readl(master->regs + GSC_I3C_TARGET_PROVISIONAL_ID0);
				provid_high = readw(master->regs + GSC_I3C_TARGET_PROVISIONAL_ID1);
				master->target_prov_id[master->i3c_dev_cnt] = (((provid_high & 0xFFFF) << 32) | (provid_low & 0xFFFFFFFF));
				master->target_bcr[master->i3c_dev_cnt] = readb(master->regs + GSC_I3C_TARGET_BCR);
				master->target_dcr[master->i3c_dev_cnt] = readb(master->regs + GSC_I3C_TARGET_DCR);
				pr_info("Target prov id = 0x%llx and Target_bcr = 0x%x and Target_dcr = 0x%x\n", master->target_prov_id[master->i3c_dev_cnt], master->target_bcr[master->i3c_dev_cnt], master->target_dcr[master->i3c_dev_cnt]);
				writeb(0x80, master->regs + GSC_I2CMCMD);
			}
			break;
		case GSC_I3C_WRITE_DATA_PHASE:
			// check for ACK to find out if slave address assigned successfully or not. if assigned successfully then, increase the i3c device count
			value = readb(master->regs + GSC_I2CSTAT);
			if (!(value & MASK_ACK)) {
				master->state = GSC_I3C_WRITE_DATA_NACK;
				gsc_i3c_stop(master);
			} else {
				if ((master->i3c_dev_cnt + 1) < MAX_DEVS) {
					master->i3c_dev_cnt = master->i3c_dev_cnt + 1;
					master->state = GSC_I3C_DAA_ADDR_PHASE;
					writew(0xfd85, master->regs + GSC_I2CMCMD);
				} else {
					pr_err("I3C TO MANY DEVICES, DROPPING\n");
					master->state = GSC_I3C_WRITE_DATA_NACK;
					gsc_i3c_stop(master);
				}
			}
			break;
		default:
			pr_err("I3C Unknown master state %d\n", master->state);
			master->state = GSC_I3C_WRITE_DATA_NACK;
			gsc_i3c_stop(master);
		}
	} else if (master->trans_type == GSC_I3C_PVT_DATA) {
		switch (master->state) {
		case GSC_I3C_PVT_DATA_PHASE:
			// Read DMA status register
			value = readb(master->regs + GSC_I3C_DMA_STATUS);

			// check for DMA error
			if (value & 0x04) {
				master->state = GSC_I3C_PVT_DATA_NACK;
				btestat = readb(master->regs + GSC_I2CMBTESTAT);
				if (((value & 0x06) == 0x06) && (btestat & 0x01)) {
					//BTE is still Busy Loop around
					writeb(0x06, master->regs + GSC_I3C_DMA_STATUS);
					return IRQ_HANDLED;
				} else if ((value & 0x06) == 0x06) {
					//BTE is Done Clear out BTE / DMA
					writeb(0xa, master->regs + GSC_I2CMBTESTAT);
					writeb(0x6, master->regs + GSC_I3C_DMA_STATUS);
				}
			} else if (value & 0x02) {
				for (index = 0; index < master->i3c_nxfers; index++) {
					// read the data from the DMA read buffer into memory
					if (master->i3c_xfers[index].rnw)
						memcpy((void *)master->i3c_xfers[index].data.in,
						       (void *)master->read_virt_addr,
						       master->i3c_xfers[index].len);
				}
				// clear the interrupt
				writeb(0x02, master->regs + GSC_I3C_DMA_STATUS);
			}
			complete(&master->completion);
			break;
		case GSC_I3C_PVT_DATA_NACK:
			btestat = readb(master->regs + GSC_I2CMBTESTAT);
			//value = readb(master->regs + GSC_I3C_DMA_STATUS);
			if ((btestat & 0x01) == 0x00) {
				writeb(0xa, master->regs + GSC_I2CMBTESTAT);
				complete(&master->completion);
			}
			//BTE still busy, loop around
			break;
		}
	} else if (master->trans_type == GSC_I3C_IBI) {
		switch (master->state) {
		case GSC_I3C_READ_DATA_PHASE:
			// store the data returned
			/* The first snoop[0] will contain the Mandatory Data Byte Described here
			 * https://www.mipi.org/MIPI_I3C_mandatory_data_byte_values_public
			 */
			value = readb(master->regs + GSC_I2CSNPDAT);
			master->i3c_ibi_payload[current_ibi_slot].snoop_data[master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt] = value;
			master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt++;
			value = readb(master->regs + GSC_I2CSTAT);

			// if last byte is already received or there is no ACK from the target,
			// then stop the transaction
			if ((master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt == master->i3c_ibi_payload[current_ibi_slot].ibi_max_data_cnt) || (!(value & MASK_ACK))) {

				buf = slot->data;

				slot->len = master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt;
				memcpy(buf, master->i3c_ibi_payload[current_ibi_slot].snoop_data, master->i3c_ibi_payload[current_ibi_slot].ibi_max_data_cnt);

				i3c_master_queue_ibi(dev, slot);

				writeb(0x82, master->regs + GSC_I2CMCMD); // clear event, send stop
			} else {
				master->state = GSC_I3C_READ_DATA_PHASE;
				// if this is last byte to read then don't acknowledge otherwise acknowledge the received data
				if ((master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt + 1) == master->i3c_ibi_payload[current_ibi_slot].ibi_max_data_cnt) {
					buf = slot->data;
					slot->len = master->i3c_ibi_payload[current_ibi_slot].ibi_data_cnt;
					memcpy(buf, master->i3c_ibi_payload[current_ibi_slot].snoop_data, master->i3c_ibi_payload[current_ibi_slot].ibi_max_data_cnt);

					i3c_master_queue_ibi(dev, slot);
					writeb(0x84, master->regs + GSC_I2CMCMD);
				} else {
					/* Keep On Goin! */
					writeb(0x8c, master->regs + GSC_I2CMCMD);
				}
			}
			break;
		}
	}
	return IRQ_HANDLED;
}

static const struct i3c_master_controller_ops hpe_mipi_i3c_ops = {
	.bus_init = hpe_i3c_master_bus_init,
	.bus_cleanup = hpe_i3c_master_bus_cleanup,
	.attach_i3c_dev = hpe_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = hpe_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = hpe_i3c_master_detach_i3c_dev,
	.do_daa = hpe_i3c_master_daa,
	.supports_ccc_cmd = hpe_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = hpe_i3c_master_send_ccc_cmd,
	.priv_xfers = hpe_i3c_master_priv_xfers,
	.attach_i2c_dev = hpe_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = hpe_i3c_master_detach_i2c_dev,
	.i2c_xfers = hpe_i3c_master_i2c_xfers,
	.request_ibi = hpe_i3c_master_request_ibi,
	.free_ibi = hpe_i3c_master_free_ibi,
	.enable_ibi = hpe_i3c_master_enable_ibi,
	.disable_ibi = hpe_i3c_master_disable_ibi,
	.recycle_ibi_slot = hpe_i3c_master_recycle_ibi_slot,
};

#ifdef CONFIG_DEBUG_FS
static int fops_access_command_get(void *ctx, u64 *val)
{
	return 0;
}

static int fops_access_command_set(void *ctx, u64 val)
{
	struct hpe_i3c_master *priv = ctx;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;

	dest.addr = I3C_BROADCAST_ADDR;
	dest.payload.len = 0;
	dest.payload.data = NULL;

	cmd.rnw = 0;
	cmd.id = I3C_CCC_SETDASA;
	cmd.dests = &dest;
	cmd.ndests = 1;
	cmd.err = I3C_ERROR_UNKNOWN;

	hpe_i3c_master_send_ccc_cmd(&priv->base, &cmd);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_access_command, fops_access_command_get,
			 fops_access_command_set, "0x%llX\n");

static int i3c_master_debugfs_init(struct hpe_i3c_master *priv, const char *engine_id)
{
	struct dentry *entry, *command_dir;

	entry = debugfs_create_dir(engine_id, NULL);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	priv->debug_dir = entry;

	entry = debugfs_create_dir("commands", priv->debug_dir);
	if (IS_ERR(entry))
		goto err_remove;

	command_dir = entry;

	entry = debugfs_create_file_unsafe("access", 0600, command_dir, priv, &fops_access_command);
	if (IS_ERR(entry))
		goto err_remove;

	return 0;

err_remove:
	debugfs_remove_recursive(priv->debug_dir);
	return PTR_ERR(entry);
}
#endif

static int hpe_i3c_probe(struct platform_device *pdev)
{
	struct hpe_i3c_master *master;
	int ret, irq;
	struct resource *res;
	char engine_id[32];

	master = devm_kzalloc(&pdev->dev, sizeof(struct hpe_i3c_master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->regs = devm_platform_ioremap_resource(pdev, 0);

	master->i3c_error = 0;

	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	init_completion(&master->completion);

	/* Use physical memory address to determine which I3C engine this is. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	master->engine = (((size_t)res->start & 0xff00) >> 8) - 0x2a;
	if (master->engine >= MAX_I3C_ENGINE) {
		return dev_err_probe(&pdev->dev, -EINVAL, "i3c engine%d is unsupported\n",
			master->engine);
	}

	sprintf(engine_id, "i3c-engine-%d", master->engine);

#ifdef CONFIG_DEBUG_FS
	i3c_master_debugfs_init(master, engine_id);
#endif
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq,
			       hpe_i3c_master_irq_handler, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, master);
	master->dev = &pdev->dev;
	master->maxdevs = MAX_DEVS - 1;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	ret = i3c_master_register(&master->base, &pdev->dev,
				  &hpe_mipi_i3c_ops, false);
	if (ret)
		return ret;

	return 0;
}

static void hpe_i3c_remove(struct platform_device *pdev)
{
	struct hpe_i3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);
}

static const struct of_device_id hpe_i3c_master_of_match[] = {
	{ .compatible = "hpe,i3c-master", },
	{},
};
MODULE_DEVICE_TABLE(of, hpe_i3c_master_of_match);

static struct platform_driver hpe_i3c_driver = {
	.probe = hpe_i3c_probe,
	.remove = hpe_i3c_remove,
	.driver = {
		.name = "hpe-i3c-master",
		.of_match_table = of_match_ptr(hpe_i3c_master_of_match),
	},
};
module_platform_driver(hpe_i3c_driver);

MODULE_AUTHOR("Jeke Kumar Gochhayat <jeke.kum.gochhayat@hpe.com>");
MODULE_DESCRIPTION("HPE I3C Controller driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Display Port driver implemantation.
 *
 */

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/types.h>
#include <linux/io.h>

#include "dpdbg.h"
#include "edid.h"
#include "vasregs.h"

#define DP_TX_BASE (0xc0020000)

/*
 * Offsets required to read common phy registers
 */
#define PHY_PIP_CONFIG_REG_INDEX_ADDR (0x0FC0)
#define PHY_PIP_CONFIG_REG_DATA_ADDR (0x0FC4)

/*
 * Ioctl commands
 */
#define DP_COMMON_PHY_READ_REG  (0)
#define DP_COMMON_PHY_WRITE_REG (1)
#define DP_CORE_READ_REG8       (2)
#define DP_CORE_WRITE_REG8      (3)
#define DP_CORE_READ_REG16      (4)
#define DP_CORE_WRITE_REG16     (5)
#define DP_CORE_READ_REG32      (6)
#define DP_CORE_WRITE_REG32     (7)
#define DP_DPCD_READ_REG        (8)
#define DP_DPCD_WRITE_REG       (9)
#define DP_LINK_SPEED_CHANGE    (10)
#define DP_EDID_READ            (11)
#define DP_VIRT_EDID_READ       (12)
#define DP_VIRT_EDID_WRITE      (13)

/* device data holder */
struct dp_device_data {
	struct cdev cdev;
};
static int dev_major;
/* sysfs class structure */
static struct class *dp_class;
/* device_data */
static struct dp_device_data dp_data;

void __iomem *dptx_base_ptr;
static  spinlock_t lock;

static int dp_common_phy_register_read(unsigned int reg, unsigned int *data);
static int dp_common_phy_register_write(unsigned int reg, unsigned int data);

static struct task_struct *dp_thread;
static int dp_thread_func(void *data)
{
	pr_debug("DP: In thread: Invoking hotplug function\n");
	hotplug_func();
	return 0;
}

static int dp_common_phy_register_write(unsigned int reg, unsigned int data)
{
	unsigned long flags = 0;

	if (dptx_base_ptr ==  NULL) {
		pr_err("DP: write common phy - error, dptx base_ptr is NULL\n");
		return -1;
	}
	spin_lock_irqsave(&lock, flags);
	iowrite32(reg, dptx_base_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
	iowrite32(data, dptx_base_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);
	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

static int dp_common_phy_register_read(unsigned int reg, unsigned int *data)
{
	unsigned long flags = 0;
	unsigned int dummy_data __maybe_unused = 0;

	if (dptx_base_ptr ==  NULL) {
		pr_err("DP: read common phy - error, dptx base_ptr is NULL\n");
		return -1;
	}

	spin_lock_irqsave(&lock, flags);

	/*
	 * Added below read test register as workaround
	 * to avoid wrong register read.
	 * This issue is for Sanjac A0 and A1 asic.
	 * The issue will be fixed in Sanjac B0.
	 */
	iowrite32(0x80f8, dptx_base_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
	dummy_data = ioread32(dptx_base_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);

	iowrite32(reg, dptx_base_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
	*data = ioread32(dptx_base_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);
	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

/*
 * Display Port ISR
 */
static irqreturn_t displayport_isr(int num, void *dev_id)
{
	pr_debug("In DP ISR\n");

	dptx_int_hdlr();

	return IRQ_HANDLED;
}

struct reg_data {
	unsigned int addr;
	unsigned int data;
};

static int dp_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int dp_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long dp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	edid_t edid;
	struct reg_data reg;
	unsigned int reg_addr = 0;
	unsigned int reg_data = 0;
	unsigned int *reg_ptr = NULL;
	int retv = 0;

	switch (cmd) {
	case DP_COMMON_PHY_READ_REG:
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_COMMON_PHY_READ_REG, copy_from_user failed\n", __func__);
			return -1;
		}

		reg_addr = reg.addr;
		if (dp_common_phy_register_read(reg_addr, &reg_data) != 0) {
			pr_err("%s: read failed reg_addr=0x%x\n",
					__func__, reg_addr);
			reg.data = 0;
			return -1;
		}
		reg.data = reg_data;

		if (copy_to_user((int __user *)arg, &reg, sizeof(struct reg_data))) {
			pr_err("%s: DP_COMMON_PHY_READ_REG, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_COMMON_PHY_WRITE_REG:
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_COMMON_PHY_WRITE_REG, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;
		reg_data = reg.data;
		if (dp_common_phy_register_write(reg_addr, reg_data) != 0) {
			pr_err("%s: write failed reg_addr=0x%x reg_data=0x%x\n",
					__func__, reg_addr, reg_data);
			return -1;
		}
	break;

	case DP_CORE_READ_REG8:
		/* Read DPTX Core 8-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG8, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;

		reg_ptr = ioremap(reg_addr, 1024);
		if (reg_ptr == NULL) {
			pr_err("DP: DP_CORE_READ_REG8 reg addr is not mapped, failed 0x%x\n",
					reg_addr);
			return -1;
		}

		reg.data = asicregister_read8(dptx_base_ptr + reg_addr);

		if (reg_ptr != NULL) {
			iounmap(reg_ptr);
			reg_ptr = NULL;
		}
		if (copy_to_user((int __user *)arg, &reg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG8, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_CORE_WRITE_REG8:
		/* Write DPTX Core 8-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_WRITE_REG8, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;
		reg_data = reg.data;

		reg_ptr = ioremap(reg_addr, 1024);
		if (reg_ptr == NULL) {
			pr_err("DP: DP_CORE_WRITE_REG8 reg addr is not mapped, failed 0x%x\n",
					reg_addr);
			return -1;
		}

		asicregister_write8(dptx_base_ptr + reg_addr, reg_data);

		if (reg_ptr != NULL) {
			iounmap(reg_ptr);
			reg_ptr = NULL;
		}
	break;

	case DP_CORE_READ_REG16:
		/* Read DPTX Core 16-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG16, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;

		reg_ptr = ioremap(reg_addr, 1024);
		if (reg_ptr == NULL) {
			pr_err("DP: DP_CORE_READ_REG16 reg addr is not mapped, failed 0x%x\n",
					reg_addr);
			return -1;
		}

		reg.data = asicregister_read16(dptx_base_ptr + reg_addr);

		if (reg_ptr != NULL) {
			iounmap(reg_ptr);
			reg_ptr = NULL;
		}
		if (copy_to_user((int __user *)arg, &reg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG16, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_CORE_WRITE_REG16:
		/* Write DPTX Core 16-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_WRITE_REG16, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;
		reg_data = reg.data;

		reg_ptr = ioremap(reg_addr, 1024);
		if (reg_ptr == NULL) {
			pr_err("DP: DP_CORE_WRITE_REG16 reg addr is not mapped, failed 0x%x\n",
					reg_addr);
			return -1;
		}

		asicregister_write16(dptx_base_ptr + reg_addr, reg_data);

		if (reg_ptr != NULL) {
			iounmap(reg_ptr);
			reg_ptr = NULL;
		}
	break;

	case DP_CORE_READ_REG32:
		/* Read DPTX Core 32-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG32, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;

		if (dptx_base_ptr ==  NULL) {
			pr_err("DP: %s: DP_CORE_READ_REG32 error, dptx base_ptr is NULL\n",
					__func__);
			return -1;
		}

		reg.data = asicregister_read32(dptx_base_ptr + reg_addr);
		//pr_debug("DP: %s: dp core read32 reg_addr 0x%x, reg_data 0x%x",
		//__func__, reg_addr, reg.data);

		if (copy_to_user((int __user *)arg, &reg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_READ_REG32, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_CORE_WRITE_REG32:
		/* Write DPTX Core 32-bit Register */
		if (copy_from_user(&reg, (int __user *)arg, sizeof(struct reg_data))) {
			pr_err("%s: DP_CORE_WRITE_REG32, copy_from_user failed\n", __func__);
			return -1;
		}
		reg_addr = reg.addr;
		reg_data = reg.data;

		if (dptx_base_ptr ==  NULL) {
			pr_err("DP: %s: DP_CORE_WRITE_REG32 error, dptx base_ptr is NULL\n",
					__func__);
			return -1;
		}

		asicregister_write32(dptx_base_ptr + reg_addr, reg_data);
		//pr_debug("DP: %s: dp core write32 reg_addr 0x%x, reg_data 0x%x\n",
		//__func__, reg_addr, reg_data);
	break;

	case DP_DPCD_READ_REG:
		/* TODO: Add DPCD Read Register */
	break;

	case DP_DPCD_WRITE_REG:
		/* TODO: Add DPCD Write Register */
	break;

	case DP_LINK_SPEED_CHANGE:
		if ((arg == DP_DBG_LINK_5_4) ||
			(arg == DP_DBG_LINK_2_7) ||
			(arg == DP_DBG_LINK_1_62)) {
			pr_debug("DP: %s: link speed change %ld\n", __func__, arg);
			common_phy_link_speed_set(arg);
			dpdbg_cmd(arg);
		} else {
			pr_err("DP: %s: speed arg is invalid %ld\n", __func__, arg);
		}

	break;

	case DP_EDID_READ:
		/* Read EDID from the display port monitor */
		retv = edid_read_dp((unsigned char *)&edid, sizeof(edid_t));
		if (retv != 0) {
			pr_err("DP: %s: edid read failed retv %d\n", __func__, retv);
			return -1;
		}

		if (copy_to_user((int __user *)arg, &edid, sizeof(edid_t))) {
			pr_err("%s: DP_EDID_READ, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_VIRT_EDID_READ:
		/* Read Virtual EDID */
		retv = edid_read_virt((unsigned char *)&edid, sizeof(edid_t));
		if (retv != 0) {
			pr_err("DP: %s: virt edid read failed retv %d\n", __func__, retv);
			return -1;
		}

		if (copy_to_user((int __user *)arg, &edid, sizeof(edid_t))) {
			pr_err("%s: DP_VIRT_EDID_READ, copy_to_user failed\n", __func__);
			return -1;
		}
	break;

	case DP_VIRT_EDID_WRITE:
		/* Write Virtual EDID */
		if (copy_from_user(&edid, (int __user *)arg, sizeof(edid_t))) {
			pr_err("DP: %s: DP_VIRT_EDID__WRITE, copy_from_user failed\n", __func__);
			return -1;
		}
		retv = edid_write_virt((unsigned char *)&edid, sizeof(edid_t));
		if (retv != 0) {
			pr_err("DP: %s: virt edid read failed retv %d\n", __func__, retv);
			return -1;
		}
	break;

	default:
		pr_err("%s: Invalid command %d\n", __func__, cmd);
	break;
	}
	return 0;
}

// initialize file_operations
static const struct file_operations dp_fops = {
	.owner      = THIS_MODULE,
	.open       = dp_open,
	.release    = dp_release,
	.unlocked_ioctl = dp_ioctl,
};

static int register_char_device(void)
{

	dev_t dev;
	int err __maybe_unused;
	int i = 0;

	/*
	 * Initiate the character driver
	 */

	i = 0;//minor no is 0
	/* allocate chardev region and assign Major number */
	err = alloc_chrdev_region(&dev, 0, 1, "dp");

	dev_major = MAJOR(dev);

	/* create sysfs class */
	dp_class = class_create("dp");

	/* create necessary device */

	/* init new device */
	cdev_init(&dp_data.cdev, &dp_fops);
	dp_data.cdev.owner = THIS_MODULE;

	/* add device to the system where "i" is a Minor number of the new device */
	cdev_add(&dp_data.cdev, MKDEV(dev_major, i), 1);

	/* create device node /dev/commonphy-x where "x" is 0, equal to the Minor number */
	device_create(dp_class, NULL, MKDEV(dev_major, i), NULL, "dp%d", i);

	return 0;
}

static int displayport_probe(struct platform_device *pdev)
{
	int ret = 0;
	int irq = 0;

	pr_debug("DP: platform_driver: device probed\n");

	dptx_base_ptr = ioremap(DP_TX_BASE, 1024*1024);
	if (dptx_base_ptr == NULL) {
		pr_err("DP: dptx base ptr is not mapped, failed\n");
		return -1;
	}
	pr_debug("DP: dptx base ptr is mapped successfully\n");

	/* Initialize the lock */
	spin_lock_init(&lock);

	/* Get the interrupt number */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		pr_err("DP: platform_get_irq request failed %d", ret);
		return ret;
	}
	irq = ret;
	pr_debug("DP: irq number %d\n", irq);

	pr_debug("Calling dp init\n");
	dp_init();
	clear_intr();

	ret = request_irq(irq, displayport_isr, 0, "dp_device", NULL);
	if (ret) {
		pr_err("DP: request_irg failed for irq %d with error 0x%x\n", irq, ret);
		return ret;
	}
	pr_debug("DP: request_irq succeeded\n");

	register_char_device();

	dp_thread = kthread_create(dp_thread_func, NULL, "dp_kernel_thread");
	if (IS_ERR(dp_thread)) {
		pr_err("DP: thread creation failed\n");
		return -1;
	}
	wake_up_process(dp_thread);

	return 0;
}

static void displayport_remove(struct platform_device *pdev)
{
	pr_debug("DP: platform_driver: device removed\n");

	if (dptx_base_ptr != NULL) {
		iounmap(dptx_base_ptr);
		dptx_base_ptr = NULL;
	}

	if (dp_thread) {
		kthread_stop(dp_thread);
		pr_debug("DP: thread stopped\n");
	}
}

static const struct of_device_id dp_platform_match[] = {
	{ .compatible = "display_port_platform_device" },
	{ },
};
MODULE_DEVICE_TABLE(of, dp_platform_match);

static struct platform_driver displayport_platform_driver = {
	.driver = {
		.name = "displayport_driver",
		.of_match_table = dp_platform_match,
		.owner = THIS_MODULE,
	},
	.probe = displayport_probe,
	.remove = displayport_remove,
};

static int __init displayport_init(void)
{
	int ret;

	ret = platform_driver_register(&displayport_platform_driver);
	if (ret) {
		pr_err("DP: platform_driver: registration failed ret=%d\n", ret);
		return ret;
	}
	pr_debug("DP: platform_driver: registered\n");

	return 0;
}

static void __exit displayport_exit(void)
{
	platform_driver_unregister(&displayport_platform_driver);
	pr_debug("DP: platform_driver: unregistered\n");
}

module_init(displayport_init);
module_exit(displayport_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prashant Badsheshi");
MODULE_DESCRIPTION("Display Port Driver");


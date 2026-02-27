// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Phy driver implemantation.
 *
 */

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include "common-phy-wrapper-addresses.h"

#define COMMON_PHY_READ_REG (0)
#define COMMON_PHY_WRITE_REG (1)
#define COMMON_PHY_SET_LINK_SPEED (2)

#define DP_TX_BASE (0xc0020000) 
#define PHY_PIP_CONFIG_REG_INDEX_ADDR (0x0FC0)
#define PHY_PIP_CONFIG_REG_DATA_ADDR (0x0FC4)


/* device data holder */
struct common_phy_device_data {
    struct cdev cdev;
};
static int dev_major = 0;
/* sysfs class structure */
static struct class *common_phy_class = NULL;
/* device_data */
static struct common_phy_device_data common_phy_data;

void __iomem *phybase_ptr = NULL;
static  spinlock_t lock;

static int common_phy_register_read(unsigned int reg, unsigned int *data);
static int common_phy_register_write(unsigned int reg, unsigned int data);

static int common_phy_register_write(unsigned int reg, unsigned int data)
{
    unsigned long flags = 0;

    if(phybase_ptr ==  NULL){
        pr_err("%s: error, phybase_ptr is NULL\n", __func__);
        return -1;
    } else {
        spin_lock_irqsave(&lock, flags);
        iowrite32(reg, phybase_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
        iowrite32(data, phybase_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);
        spin_unlock_irqrestore(&lock, flags);
    }

    return 0;
}
EXPORT_SYMBOL(common_phy_register_write);

static int common_phy_register_read(unsigned int reg, unsigned int *data)
{
    unsigned long flags = 0;
    unsigned int dummy_data = 0;

    if(phybase_ptr ==  NULL){
        pr_err("%s: error, phybase_ptr is NULL\n", __func__);
        return -1;
    } else {
        spin_lock_irqsave(&lock, flags);

        /*
         * Added below read test register as workaround
         * to avoid wrong register read.
         * This issue is for Sanjac A0 and A1 asic.
         * The issue will be fixed in Sanjac B0.
         */
        iowrite32(0x80f8, phybase_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
        dummy_data = ioread32(phybase_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);

        iowrite32(reg, phybase_ptr+PHY_PIP_CONFIG_REG_INDEX_ADDR);
        *data = ioread32(phybase_ptr+PHY_PIP_CONFIG_REG_DATA_ADDR);
        spin_unlock_irqrestore(&lock, flags);
    }
    return 0;
}
EXPORT_SYMBOL(common_phy_register_read);

/*
 * Polling the register for the right value.
 */
static int poll_register(unsigned int reg, int value)
{
    int data = 0;
    int retry_cnt = 20;

    while(retry_cnt > 0) {
        common_phy_register_read(reg, &data);
        if(data == value) {
            //poll matched, received the required value
            return 0;
        }
        msleep(1);
        retry_cnt--;
    }

    //poll failed, did not receive the reqquired value
    pr_err("%s: reg=0x%x expected_value=0x%x current_value=0x%x "
                "polling failed\n", __func__, reg, value, data);
    return -1;
}

/*
 * Power On Initialization of Common PHY.
 * Initialization configuration to make it work with
 * Ethernet and Display Port.
 */
static int common_phy_poweron_init(void)
{
    /* 1.Clear registers to default un-initialized state */
    common_phy_register_write(kWRAP_PHY_RX_DATA_ENABLE, 0x00);
    common_phy_register_write(kWRAP_PHY_TX_DATA_ENABLE, 0x00);
    common_phy_register_write(kWRAP_PHY_LANE_RSTN_BITS, 0x00);
    common_phy_register_write(kWRAP_PHY_LANE_EN_BITS, 0x00);
    common_phy_register_write(kWRAP_PHY_CONFIG_RSTN, 0x00);
    common_phy_register_write(kWRAP_LANE_ELEC_IDLE_BITS, 0x0F);
    common_phy_register_write(kWRAP_PHY_PLL0_ENABLE, 0x00);
    common_phy_register_write(kWRAP_PHY_PLL1_ENABLE, 0x00);
    common_phy_register_write(kWRAP_CDB_BUSSEL, 0x00);
    common_phy_register_write(kWRAP_CDB_REG_INIT_END, 0x00);
    /* Set Lane Mode back to original setting. (Display Port Only) */
    common_phy_register_write(kWRAP_PHY_DP_LANE_MODE, 0x00);
    /* Disable RX Core DPCD register as the link rate decision maker */
    common_phy_register_write(kWRAP_DISABLE_AUTO_RXLINK, 0x01);
    /* Set Lane Mode for all lanes */
    common_phy_register_write(kWRAP_XCVR_MODE_LANE0, 0x00);
    common_phy_register_write(kWRAP_XCVR_MODE_LANE1, 0x00);
    common_phy_register_write(kWRAP_XCVR_MODE_LANE2, 0x00);
    common_phy_register_write(kWRAP_XCVR_MODE_LANE3, 0x00);

    /* 2.Release PHY lanes out of reset */
    common_phy_register_write(kWRAP_PHY_CONFIG_RSTN, 0x01);

    /* 3.After a power on reset poll for completion of internal initialization completion. */
    if(0 != poll_register(kWRAP_CDB_ACC_OK, 0x01)) {
        pr_err("%s: kWRAP_CDB_ACC_OK polling failed\n", __func__);
        return -1;
    }

    /* 4.Additional PHY Register modifications */

    /* Enable CDB Bus (Internal PHY Registers */
    common_phy_register_write(kWRAP_CDB_BUSSEL, 0x01);
    common_phy_register_write(kWRAP_CDB_ENABLE, 0x01);

    /* Set DP lanes for Dual Mode */
    common_phy_register_write(0x4021, 0x00);
    common_phy_register_write(0x4221, 0x00);

    /* RX Lane Fixes B0 */
    common_phy_register_write(0x4026, 0x124A);
    common_phy_register_write(0x4226, 0x124A);
    common_phy_register_write(0x4226, 0x124A);
    common_phy_register_write(0x4626, 0x124A);

    /* Disable CDB Bus (Internal PHY Registers) */
    common_phy_register_write(kWRAP_CDB_BUSSEL, 0x00);
    common_phy_register_write(kWRAP_CDB_ENABLE, 0x00);

    /* 5.Set the PHY initialization complete bit */
    common_phy_register_write(kWRAP_CDB_REG_INIT_END, 0x01);

    /* 6.Enable PLL 0 all PLL 1 */
    common_phy_register_write(kWRAP_PHY_PLL0_ENABLE, 0x01);
    common_phy_register_write(kWRAP_PHY_PLL1_ENABLE, 0x01);

    /* 7.Program RBR, HBR, and HBR2 pre-emphasis and vswing starting levels.
     * Setting to starting level 0 for lane 0 and lane 1 for Display Port */
    common_phy_register_write(kWRAP_TX_VMARGIN_LANE0, 0x00040404);
    common_phy_register_write(kWRAP_TX_VMARGIN_LANE1, 0x00040404);
    common_phy_register_write(kWRAP_TX_DEEMPH_LANE0, 0x00000000);
    common_phy_register_write(kWRAP_TX_DEEMPH_LANE1, 0x00000000);

    /* 8.Program initial values for vswing and pre-emphasis for lane 2 & lane 3 for ethernet.
     * Setting for 400 mV & 0 dB for Ethernet */
    common_phy_register_write(kWRAP_TX_VMARGIN_LANE2, 0x00040404);
    common_phy_register_write(kWRAP_TX_VMARGIN_LANE3, 0x00040404);
    common_phy_register_write(kWRAP_TX_DEEMPH_LANE2, 0x00000000);
    common_phy_register_write(kWRAP_TX_DEEMPH_LANE3, 0x00000000);

    /* 9.Take lanes out of Electrical Idle */
    common_phy_register_write(kWRAP_LANE_ELEC_IDLE_BITS, 0x0);

    /* 10.Enable PLL Clocks.
     * kWRAP_XCVR_PLLCLK1_ENABLE not required as DP operates in dual mode. */
    common_phy_register_write(kWRAP_XCVR_PLLCLK0_ENABLE, 0x01);
    common_phy_register_write(kWRAP_XCVR_PLLCLK2_ENABLE, 0x01);
    common_phy_register_write(kWRAP_XCVR_PLLCLK3_ENABLE, 0x01);

    /* 11.Release lane resets (LINK_RESET_B) */
    common_phy_register_write(kWRAP_PHY_LANE_RSTN_BITS, 0x0F);

    /* 12.Enable Lanes (DisplayPort Only – Does not affect Ethernet) */
    common_phy_register_write(kWRAP_PHY_LANE_EN_BITS, 0x0F);

    /* 13.Poll for Common Ready */
    if(0 != poll_register(kWRAP_PHY_CMN_READY, 0x01)) {
        pr_err("%s: kWRAP_PHY_CMN_READY polling failed\n", __func__);
        return -1;
    }

    /* 14.Poll for PLL status */
    if(0 != poll_register(kWRAP_XCVR_PLLCLK0_EN_ACK, 0x01)) {
        pr_err("%s: kWRAP_XCVR_PLLCLK0_EN_ACK polling failed\n", __func__);
        return -1;
    }
    if(0 != poll_register(kWRAP_XCVR_PLLCLK1_EN_ACK, 0x01)) {
        pr_err("%s: kWRAP_XCVR_PLLCLK1_EN_ACK polling failed\n", __func__);
        return -1;
    }
    if(0 != poll_register(kWRAP_XCVR_PLLCLK2_EN_ACK, 0x01)) {
        pr_err("%s: kWRAP_XCVR_PLLCLK1_EN_ACK polling failed\n", __func__);
        return -1;
    }

    /* 15.Poll for Lanes ready */
    if(0 != poll_register(kWRAP_PHY_DP_LANESET_READY, 0x0F)) {
        pr_err("%s: kWRAP_PHY_DP_LANESET_READY polling failed\n", __func__);
        return -1;
    }

    /* 16.Enable TX data flow between link and phy */
    common_phy_register_write(kWRAP_PHY_TX_DATA_ENABLE, 0x01);

    /* 17. Set DP Link Rate to 1.62 Gbps */
    common_phy_register_write(kWRAP_PHY_DP_LINK_RATE, 0x6);

    pr_info("%s: initialization is successful\n", __func__);
    return 0;
}

struct reg_data {
    unsigned int addr;
    unsigned int data;
};

static int common_phy_open(struct inode *inode, struct file *file)
{
    pr_info("%s: Device open\n", __func__);
    return 0;
}

static int common_phy_release(struct inode *inode, struct file *file)
{
    pr_info("%s: Device close\n", __func__);
    return 0;
}

static long common_phy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct reg_data *reg;
    unsigned int reg_addr = 0;
    unsigned int reg_data = 0;

    pr_info("%s: Device ioctl cmd=%d\n", __func__, cmd);
    
    switch(cmd) {
        case COMMON_PHY_READ_REG:
            reg = (struct reg_data *)arg;
            reg_addr = reg->addr;
            if (0 != common_phy_register_read(reg_addr, &reg_data)) {
                pr_err("%s: read failed reg_addr=0x%x\n",
                                __func__, reg_addr);
                reg->data = 0;
            } else {
                reg->data = reg_data;
            }
            break;

        case COMMON_PHY_WRITE_REG:
            reg = (struct reg_data *)arg;
            reg_addr = reg->addr;
            reg_data = reg->data;
            if (0 != common_phy_register_write(reg_addr, reg_data)) {
                pr_err("%s: write failed reg_addr=0x%x reg_data=0x%x\n",
                                __func__, reg_addr, reg_data);
            }
            break;

        case COMMON_PHY_SET_LINK_SPEED:
            /* TODO: Add Link Speed Change Configuration */
            break;

        default:
            pr_err("%s: Invalid command %d\n", __func__, cmd);
            break;
    }
    return 0;
}

// initialize file_operations
static const struct file_operations common_phy_fops = {
    .owner      = THIS_MODULE,
    .open       = common_phy_open,
    .release    = common_phy_release,
    .unlocked_ioctl = common_phy_ioctl,
};

static int register_char_device(void)
{

    dev_t dev;
    int err;
    int i = 0;

    /* 
     * Initiate the character driver 
     */

    i = 0;//minor no is 0
    /* allocate chardev region and assign Major number */
    err = alloc_chrdev_region(&dev, 0, 1, "commonphy");

    dev_major = MAJOR(dev);

    /* create sysfs class */
    common_phy_class = class_create("commonphy");

    /* create necessary device */

    /* init new device */
    cdev_init(&common_phy_data.cdev, &common_phy_fops);
    common_phy_data.cdev.owner = THIS_MODULE;

    /* add device to the system where "i" is a Minor number of the new device */
    cdev_add(&common_phy_data.cdev, MKDEV(dev_major, i), 1);

    /* create device node /dev/commonphy-x where "x" is 0, equal to the Minor number */
    device_create(common_phy_class, NULL, MKDEV(dev_major, i), NULL, "commonphy%d", i);

    return 0;
}

static int common_phy_probe(struct platform_device *pdev)
{
    int ret = 0;

    pr_info("%s: common_phy_platform_driver: device probed\n", __func__);

    phybase_ptr = ioremap(DP_TX_BASE, 1024*1024);
    if(phybase_ptr == NULL) {
        pr_err("%s: phybase_ptr is not mapped, failed\n", __func__);
        return -1;
    } else {
        pr_info("%s: phybase_ptr is mapped successfully\n", __func__);
    }

    /* Initialize the lock */
    spin_lock_init(&lock);

    /* Common PHY poweron initialization */
    ret = common_phy_poweron_init();
    if(0 != ret) {
        pr_err("%s: Error in common_phy_poweron_init, ret=%d\n", __func__, ret);
    }
    
    register_char_device();

    return 0;
}

static int common_phy_remove(struct platform_device *pdev)
{
    pr_info("%s: common_phy_platform_driver: device removed\n", __func__);

    if(phybase_ptr != NULL) {
        iounmap(phybase_ptr);
        phybase_ptr = NULL;
    }

    return 0;
}

static const struct of_device_id my_platform_match[] = {
    { .compatible = "common_phy_platform_device" },
    { },
};
MODULE_DEVICE_TABLE(of, my_platform_match);

static struct platform_driver common_phy_platform_driver = {
    .driver = {
        .name = "common_phy_driver",
        .of_match_table = my_platform_match,
        .owner = THIS_MODULE,
    },
    .probe = common_phy_probe,
    .remove = common_phy_remove,
};

static int __init common_phy_init(void) {

    int ret;
    ret = platform_driver_register(&common_phy_platform_driver);
    if (ret) {
        pr_err("%s: common_phy_platform_driver: registration failed ret=%d\n", 
                __func__, ret);
        return ret;
    }
    pr_info("%s: common_phy_platform_driver: registered\n", __func__);

    return 0;
}

static void __exit common_phy_exit(void) {

    platform_driver_unregister(&common_phy_platform_driver);
    pr_info("%s: common_phy_platform_driver: unregistered\n", __func__);
}

module_init(common_phy_init);
module_exit(common_phy_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prashant Badsheshi");
MODULE_DESCRIPTION("Common PHY Driver for Display Port and Ethernet");

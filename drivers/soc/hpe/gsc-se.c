// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/regmap.h>
#include <linux/of_reserved_mem.h>
#include "linux/gxp-soclib.h"

#define DEVICE_NAME "enclave"
#define DEVICE_COUNT 1

#define EMRBASE 0xCEFD0000
#define EMEND 0xCEFE0000

#define EMI_BUF 0x8000
#define EMO_BUF 0xc000

#define EMI_DBELL 0x0000
#define EMI_STS 0x0001
#define EMI_IEN 0x0002
#define EMSG_CFG 0x0003
#define EMO_DBELL 0x0004
#define EMO_STS 0x0005
#define EMO_IEN 0x0006

#define IO_BUFFER_SIZE (0x4000)

struct enclave_drvdata {
	struct device *dev;
	void __iomem *base;
	struct regmap *se_regmap;

	struct cdev enclave_cdev;
	dev_t devid;
	int irq;
	spinlock_t lock;

	wait_queue_head_t waitq_r;
	wait_queue_head_t waitq_w;
	wait_queue_head_t waitq_p;

	bool data_in_available;
	bool data_out_available;
	bool part_reset;

	uint8_t data_out_buffer[IO_BUFFER_SIZE];
	uint8_t data_in_buffer[IO_BUFFER_SIZE];
};

static void enclave_enable_cmd_dbell_ack_irq(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val |= 0x02;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}
static void enclave_enable_cmd_rsp_dbell_irq(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val |= 0x01;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}
static void enclave_enable_part_rst_irqs(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val |= 0x04;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}

static void enclave_disable_cmd_dbell_ack_irq(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val &= ~0x02;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}
static void enclave_disable_cmd_rsp_dbell_irq(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val &= ~0x01;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}

static void enclave_disable_part_rst_irq(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_IEN, &val);
	val &= ~0x04;
	regmap_write(drvdata->se_regmap, EMO_IEN, val);
}

static void enclave_set_cmd_doorbell(struct enclave_drvdata *drvdata)
{
	regmap_write(drvdata->se_regmap, EMI_DBELL, 1);
}

static void enclave_clear_rsp_doorbell(struct enclave_drvdata *drvdata)
{
	regmap_write(drvdata->se_regmap, EMO_DBELL, 1);
}


static void enclave_clear_cmd_dbell_ack(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_STS, &val);
	val |= 0x02;
	regmap_write(drvdata->se_regmap, EMO_STS, val);
}

static void enclave_clear_rsp_dbell_sts(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_STS, &val);
	val |= 0x01;
	regmap_write(drvdata->se_regmap, EMO_STS, val);
}

static void enclave_clear_cmd_part_rst(struct enclave_drvdata *drvdata)
{
	uint32_t val;
	regmap_read(drvdata->se_regmap, EMO_STS, &val);
	val |= 0x04;
	regmap_write(drvdata->se_regmap, EMO_STS, val);
}

static void enclave_init(struct enclave_drvdata *drvdata)
{
	uint32_t doorbell;

	enclave_clear_cmd_dbell_ack(drvdata);

	drvdata->data_out_available = true;

	regmap_read(drvdata->se_regmap, EMO_DBELL, &doorbell);
	if (doorbell == 1) {
		enclave_clear_rsp_doorbell(drvdata);
	}
	drvdata->data_in_available = false;

	drvdata->part_reset = false;

	regmap_write(drvdata->se_regmap, EMO_IEN, 0);

	enclave_enable_cmd_rsp_dbell_irq(drvdata);
	enclave_disable_cmd_dbell_ack_irq(drvdata);
	enclave_enable_part_rst_irqs(drvdata);
}

static int enclave_open(struct inode *inode, struct file *file)
{
	struct enclave_drvdata *drvdata;

	drvdata = container_of(inode->i_cdev, struct enclave_drvdata,
			       enclave_cdev);

	file->private_data = drvdata;
	enclave_init(drvdata);
	if (file->f_flags & O_NONBLOCK) {
		return -EAGAIN;
	}

	return 0;
}
static ssize_t enclave_read(struct file *file, char __user *buf, size_t count,
			    loff_t *f_pos)
{
	struct enclave_drvdata *drvdata =
		(struct enclave_drvdata *)file->private_data;

	unsigned long flags;
	int ret_val = 0;

	if (count != IO_BUFFER_SIZE) {
		dev_err(drvdata->dev, "incorrect read length %zu\n", count);
		return -EINVAL;
	}

	if (!drvdata->data_in_available && (file->f_flags & O_NONBLOCK)) {
		if (drvdata->part_reset) {
			enclave_init(drvdata);
			return -EIO;
		}
		return -EAGAIN;
	}
	ret_val = wait_event_interruptible(drvdata->waitq_r,
					   drvdata->data_in_available);
	if (ret_val) {
		return ret_val;
	}

	if (drvdata->part_reset) {
		enclave_init(drvdata);
		return -EIO;
	}

	ret_val = regmap_bulk_read(drvdata->se_regmap, EMO_BUF,
				   (void *)drvdata->data_in_buffer,
				   IO_BUFFER_SIZE);
	if (ret_val) {
		dev_err(drvdata->dev, "error: failed to read inbound buffer\n");
		return ret_val;
	}

	if (copy_to_user(buf, (void *)drvdata->data_in_buffer,
			 IO_BUFFER_SIZE)) {
		dev_err(drvdata->dev, "error:failed copy_to_user\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&drvdata->lock, flags);
	drvdata->data_in_available = false;
	enclave_clear_rsp_doorbell(drvdata);
	enclave_enable_cmd_rsp_dbell_irq(drvdata);
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return IO_BUFFER_SIZE;
}

static ssize_t enclave_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	struct enclave_drvdata *drvdata =
		(struct enclave_drvdata *)file->private_data;
	int ret_val;
	unsigned long flags;
	uint32_t val = 0;

	if (count != IO_BUFFER_SIZE) {
		dev_err(drvdata->dev, "error: incorrect write len %zu\n",
			count);
		return -EINVAL;
	}
	if (!drvdata->data_out_available && (file->f_flags & O_NONBLOCK)) {
		if (drvdata->part_reset) {
			enclave_init(drvdata);
			return -EIO;
		}
		return -EAGAIN;
	}

	ret_val = wait_event_interruptible(drvdata->waitq_w,
					   drvdata->data_out_available);
	if (ret_val) {
		return ret_val;
	}
	if (drvdata->part_reset) {
		enclave_init(drvdata);
		return -EIO;
	}
	if (copy_from_user(drvdata->data_out_buffer, buf, count)) {
		dev_err(drvdata->dev, "error: copy_from_user() failed\n");
		return -EIO;
	}
	ret_val = regmap_bulk_write(drvdata->se_regmap, EMI_BUF,
				    (void *)drvdata->data_out_buffer,
				    IO_BUFFER_SIZE);
	if (ret_val) {
		dev_err(drvdata->dev,
			"erroe: failed to write outbound buffer\n");
		return ret_val;
	}
	ret_val = regmap_read_poll_timeout(drvdata->se_regmap, EMO_DBELL, val,
					   val == 0, 100, 5000000); // 5 seconds

	if (ret_val) {
		dev_err(drvdata->dev, "Wait for EMO_DBELL clear timeout\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&drvdata->lock, flags);
	drvdata->data_out_available = false;
	enclave_enable_cmd_dbell_ack_irq(drvdata);
	enclave_set_cmd_doorbell(drvdata);
	spin_unlock_irqrestore(&drvdata->lock, flags);

	return IO_BUFFER_SIZE;
}

static __poll_t enclave_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct enclave_drvdata *drvdata =
		(struct enclave_drvdata *)file->private_data;

	poll_wait(file, &drvdata->waitq_p, wait);

	if (drvdata->data_in_available) {
		mask |= (EPOLLIN | EPOLLRDNORM);
	}
	if (drvdata->data_out_available) {
		mask |= (EPOLLOUT | EPOLLWRNORM);
	}
	return mask;
}

static int enclave_release(struct inode *inode, struct file *file)
{
	uint32_t doorbell;
	uint32_t status;
	uint32_t ien;

	struct enclave_drvdata *drvdata;

	drvdata = container_of(inode->i_cdev, struct enclave_drvdata,
			       enclave_cdev);

	file->private_data = drvdata;

	regmap_read(drvdata->se_regmap, EMO_DBELL, &doorbell);
	regmap_read(drvdata->se_regmap, EMO_STS, &status);
	regmap_read(drvdata->se_regmap, EMO_IEN, &ien);

	return 0;
}

static const struct file_operations enclave_fops = {
	.open = enclave_open,
	.read = enclave_read,
	.write = enclave_write,
	.poll = enclave_poll,
	.llseek = noop_llseek,
	.release = enclave_release,
};

static irqreturn_t enclave_irq_handler(int irq, void *_drvdata)
{
	struct enclave_drvdata *drvdata = (struct enclave_drvdata *)_drvdata;
	uint32_t doorbell;
	uint32_t status;
	uint32_t ien;

	bool flag = false;

	regmap_read(drvdata->se_regmap, EMO_DBELL, &doorbell);
	regmap_read(drvdata->se_regmap, EMO_STS, &status);
	regmap_read(drvdata->se_regmap, EMO_IEN, &ien);

	if (((status & 0x02) == 0x02) && ((ien & 0x02) == 0x02)) {
		// enclave consumed the cmd buffer, it is ours again
		enclave_disable_cmd_dbell_ack_irq(drvdata);

		enclave_clear_cmd_dbell_ack(drvdata);

		drvdata->data_out_available = true;

		wake_up_interruptible(&drvdata->waitq_w);
		wake_up_interruptible(&drvdata->waitq_p);

		flag = true;
	}
	if (((status & 0x01) == 0x01) && ((ien & 0x01) == 0x01)) {
		enclave_disable_cmd_rsp_dbell_irq(drvdata);

		enclave_clear_rsp_dbell_sts(drvdata);

		drvdata->data_in_available = true;

		wake_up_interruptible(&drvdata->waitq_r);
		wake_up_interruptible(&drvdata->waitq_p);

		flag = true;
	}
	if (((status & 0x04) == 0x04) && ((ien & 0x04) == 0x04)) {
		enclave_disable_part_rst_irq(drvdata);

		enclave_clear_cmd_part_rst(drvdata);
		drvdata->part_reset = true;
		wake_up_interruptible(&drvdata->waitq_r);
		wake_up_interruptible(&drvdata->waitq_w);
		wake_up_interruptible(&drvdata->waitq_p);

		flag = true;
	}
	if (!flag) {
		printk("Interrupt for some other condition\n");
	}
	return IRQ_HANDLED;
}
static const struct regmap_config enclave_regmap_cfg = {
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
	.max_register = EMEND,
};

static int enclave_probe(struct platform_device *pdev)
{
	struct enclave_drvdata *drvdata;
	struct resource *res;
	int irq;
	int rc;
	int major, minor;
	struct device *dev;
	uint32_t doorbell;
	uint32_t status;
	uint32_t ien;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (IS_ERR_OR_NULL(drvdata))
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	drvdata->dev = &pdev->dev;

	/* map Enclave register configuration space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "error: platform_get_resource()\n");
		return -ENOMEM;
	}

	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (!drvdata->base) {
		dev_err(&pdev->dev, "error: failed devm_ioremap");
		return -ENOMEM;
	}

	drvdata->se_regmap = devm_regmap_init_mmio(&pdev->dev, drvdata->base,
						   &enclave_regmap_cfg);
	if (IS_ERR(drvdata->se_regmap)) {
		dev_err(&pdev->dev, "error: failed devm_regmap_init_mmio()\n");
		return PTR_ERR(drvdata->se_regmap);
	}

	of_reserved_mem_device_init(drvdata->dev);

	rc = dma_set_mask_and_coherent(drvdata->dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "Failed to set DMA mask\n");
		of_reserved_mem_device_release(drvdata->dev);
	}

	/* get Linux IRQ number */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return irq;
	}

	drvdata->irq = irq;

	rc = devm_request_irq(drvdata->dev, drvdata->irq, enclave_irq_handler,
			      IRQF_SHARED, DEVICE_NAME, drvdata);
	if (rc < 0) {
		dev_err(&pdev->dev, "irq request failed\n");
		return irq;
	}

	rc = alloc_chrdev_region(&drvdata->devid, 0, DEVICE_COUNT, DEVICE_NAME);
	if (rc < 0) {
		dev_err(drvdata->dev, "unable to alloc char device\n");
		rc = -ENODEV;
		return rc;
	}

	major = MAJOR(drvdata->devid);
	minor = 1;
	dev_info(drvdata->dev, "Enclave major=%d minor=%d\n", major, minor);

	cdev_init(&drvdata->enclave_cdev, &enclave_fops);
	drvdata->enclave_cdev.owner = THIS_MODULE;

	rc = cdev_add(&drvdata->enclave_cdev, MKDEV(major, minor), 1);
	if (rc < 0) {
		dev_err(drvdata->dev, "unable to add char device\n");
		goto unregister;
	}

	dev = device_create(soc_class, drvdata->dev, MKDEV(major, minor),
			    drvdata, DEVICE_NAME "%d", 1);
	if (IS_ERR(dev)) {
		dev_err(drvdata->dev, "unable to create sysfs\n");
		goto unregister;
	}

	regmap_read(drvdata->se_regmap, EMO_DBELL, &doorbell);
	regmap_read(drvdata->se_regmap, EMO_STS, &status);
	regmap_read(drvdata->se_regmap, EMO_IEN, &ien);

	spin_lock_init(&drvdata->lock);

	init_waitqueue_head(&drvdata->waitq_r);
	init_waitqueue_head(&drvdata->waitq_w);
	init_waitqueue_head(&drvdata->waitq_p);

	enclave_clear_cmd_dbell_ack(drvdata);

	drvdata->data_out_available = true;

	enclave_clear_rsp_doorbell(drvdata);
	drvdata->data_in_available = false;

	drvdata->part_reset = false;

	dev_info(&pdev->dev, "Secure Enclave device ready\n");

	enclave_enable_cmd_rsp_dbell_irq(drvdata);
	return 0;

unregister:
	unregister_chrdev_region(drvdata->devid, DEVICE_COUNT);
	return rc;
}

static void enclave_remove(struct platform_device *pdev)
{
	struct enclave_drvdata *drvdata;

	drvdata = (struct enclave_drvdata *)platform_get_drvdata(pdev);
	of_reserved_mem_device_release(drvdata->dev);

	cdev_del(&drvdata->enclave_cdev);
	unregister_chrdev_region(drvdata->devid, DEVICE_COUNT);
	free_irq(drvdata->irq, drvdata);
}

static const struct of_device_id enclave_of_match[] = {
	{ .compatible = "hpe,gsc-se" },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_xreg_of_match);

static struct platform_driver enclave_driver = {
	.probe = enclave_probe,
	.remove = enclave_remove,
		.driver = {
		.name = "gsc-se",
		.of_match_table = of_match_ptr(enclave_of_match),
	},
};
module_platform_driver(enclave_driver);

MODULE_AUTHOR("Ivan Farkas<ivan.farkas@hpe.com");
MODULE_DESCRIPTION("Secure Enclave Driver");

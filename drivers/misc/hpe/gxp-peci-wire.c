// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021-2025 Hewlett Packard Enterprise Development LP
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/reset.h>
#include <linux/peci-wire-ioctl.h>

#define PECI_WIRE_CMD		0x0000
#define PECI_WIRE_STAT		0x0004
#define PECI_WIRE_TCFG		0x0008
#define PECI_WIRE_CFG		0x000C
#define PECI_WIRE_DATA		0x0100

#define PECI_WIRE_CMD_RD_LEN_POS 		24
#define PECI_WIRE_CMD_WT_LEN_POS 		16
#define PECI_WIRE_CMD_TGT_POS 		    8
#define PECI_WIRE_CMD_START 		    1
#define PECI_WIRE_CMD_SWRESET 		    0x80

#define PECI_WIRE_TIM_NEG_FAIL          0x00001000
#define PECI_WIRE_BIT_COLL_FAIL         0x00000800
#define PECI_WIRE_FCS_RD_FAIL           0x00000400
#define PECI_WIRE_FCS_WT_FAIL           0x00000200
#define PECI_WIRE_FCS_CA_FAIL           0x00000100
#define PECI_WIRE_DONE                  0x00000001

#define PECI_WIRE_FAIL_MASK            (PECI_WIRE_TIM_NEG_FAIL  | \
                                        PECI_WIRE_BIT_COLL_FAIL | \
                                        PECI_WIRE_FCS_RD_FAIL   | \
                                        PECI_WIRE_FCS_WT_FAIL   | \
                                        PECI_WIRE_FCS_CA_FAIL)

#define PECI_IOCTL_MAX_RETRY           20

struct peci_wire_drvdata {
    struct platform_device    *pdev;
    struct miscdevice          mdev;
    void __iomem              *base;
    uint8_t                    node;
    uint32_t                   rd_offset;
    struct peci_wire_req_t     pw_req;
    uint8_t                    response;
    uint8_t                    dev_open;
};

DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static int debug_flag = 0;

static inline struct peci_wire_drvdata *to_peci_wire_drvdata(struct file *file)
{
    struct miscdevice *miscdev = file->private_data;
    return container_of(miscdev, struct peci_wire_drvdata, mdev);
}

static void peci_wire_reset(struct peci_wire_drvdata *pw_data)
{

    writel(PECI_WIRE_CMD_SWRESET, pw_data->base + PECI_WIRE_CMD);
    return;
}


static void peci_wire_cmd_issue(struct peci_wire_drvdata *pw_data, struct peci_wire_req_t *preq)
{
    uint32_t command = 0;
    int i;

    // Sanity check inputs

    // Write command to PECIDATA
    for (i = 0; i < preq->wr_len; i++)
    {
        if (debug_flag)
        {
            dev_info(&pw_data->pdev->dev, "Wr Buf[%d]: %02x", i, preq->wr_buf[i]);
        }
        writeb(preq->wr_buf[i], pw_data->base + PECI_WIRE_DATA + i);
    }

    // Set the response offset in the data buffer
    if (preq->wr_len < 0x78)
    {
        pw_data->rd_offset = 0x80;
    }
    else
    {
        pw_data->rd_offset = preq->wr_len + 8;
    }
    writel(pw_data->rd_offset, pw_data->base + PECI_WIRE_CFG);

    // Write length fields and start bit to PECICMD
    command = (uint32_t)(preq->rd_len << PECI_WIRE_CMD_RD_LEN_POS) |
               (uint32_t)(preq->wr_len << PECI_WIRE_CMD_WT_LEN_POS) |
               (uint32_t)(preq->target << PECI_WIRE_CMD_TGT_POS) |
               PECI_WIRE_CMD_START;
    if (debug_flag)
    {
        dev_info(&pw_data->pdev->dev, "command %08x", command);
    }
    writel(command, pw_data->base + PECI_WIRE_CMD);
    return;
}


static long int peci_wire_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *user = (void __user *)arg;
    struct peci_wire_req_t *user_pw_req;
    struct peci_wire_drvdata *pw_data = to_peci_wire_drvdata(file);
    int rc;
    uint32_t stat_reg;
    int count;

    switch (_IOC_NR(cmd))
    {
        case PECI_IOCTL_ISSUE_CMD:
            user_pw_req = (struct peci_wire_req_t *)user;
            if (debug_flag)
            {
                dev_info(&pw_data->pdev->dev, "IOCTL issue cmd start");
            }
            memset(&pw_data->pw_req, 0, sizeof(struct peci_wire_req_t));

            if (!access_ok(user, _IOC_SIZE(cmd))) {
                dev_err(&pw_data->pdev->dev, "IOCTL access to user space failed");
                return  -EFAULT;
            }

            if (copy_from_user((void *)&pw_data->pw_req, (void *)arg, _IOC_SIZE(cmd))) {
                dev_err(&pw_data->pdev->dev, "IOCTL copy from user space failed");
                return  -EFAULT;
            }
            pw_data->response = 0;
            count = PECI_IOCTL_MAX_RETRY;
            do {
                if (debug_flag)
                {
                    dev_info(&pw_data->pdev->dev, "IOCTL issuing command");
                }

                peci_wire_cmd_issue(pw_data, &pw_data->pw_req);

                // wait on irq
                if (debug_flag)
                {
                    dev_info(&pw_data->pdev->dev, "IOCTL waiting");
                }

                rc = wait_event_interruptible_timeout(wait_queue, pw_data->response > 0,
                                                        msecs_to_jiffies(100));
                if (rc == 0)
                {
                    dev_err(&pw_data->pdev->dev, "IOCTL timed out (%d)", rc);
                    // read status
                    stat_reg = readl(pw_data->base + PECI_WIRE_STAT);
                    dev_err(&pw_data->pdev->dev, "STAT = 0x%08x", stat_reg);

                    // write back PECISTAT to clear any errors
                    writel(stat_reg, pw_data->base + PECI_WIRE_STAT);
                }
                count--;
            } while ((rc < 1) && (count > 0));

            if (count == 0)
            {
                dev_err(&pw_data->pdev->dev, "IOCTL retries exceeded");
                return -EFAULT;
            }

            if (debug_flag)
            {
                dev_info(&pw_data->pdev->dev, "IOCTL completing");
            }
            // copy data back to user
            if (copy_to_user(user_pw_req->rd_buf, (void *)&pw_data->pw_req.rd_buf, pw_data->pw_req.rd_len)) {
                dev_err(&pw_data->pdev->dev, "IOCTL copy buffer to user space failed");
                return  -EFAULT;
            }
            if (copy_to_user(&user_pw_req->status, (void *)&pw_data->pw_req.status, sizeof(pw_data->pw_req.status))) {
                dev_err(&pw_data->pdev->dev, "IOCTL copy status to user space failed");
                return  -EFAULT;
            }

            break;
        case PECI_IOCTL_RESET_CMD:
            if (debug_flag)
            {
                dev_info(&pw_data->pdev->dev, "GXP PECI originator reset");
            }
            peci_wire_reset(pw_data);
            break;
        default:
            dev_err(&pw_data->pdev->dev, "Unrecognized IOCTL (%d)", _IOC_NR(cmd));
            return -EINVAL;
            break;
    }
    return 0;
}

static irqreturn_t peci_wire_irq_handler(int num, void *dev_id)
{
    struct peci_wire_drvdata *pw_data = dev_id;
    uint32_t stat_reg;
    int i;

    if (debug_flag)
    {
        dev_info(&pw_data->pdev->dev, "IRQ received");
    }

    // read status
    stat_reg = readl(pw_data->base + PECI_WIRE_STAT);

    // write back PECISTAT to clear Done and any errors
    writel(stat_reg, pw_data->base + PECI_WIRE_STAT);

    // read check for errrors
    pw_data->pw_req.status = PECI_WIRE_STAT_SUCCESS;
    if (stat_reg & PECI_WIRE_FAIL_MASK)
    {
        if (stat_reg & PECI_WIRE_TIM_NEG_FAIL)
        {
            dev_err(&pw_data->pdev->dev, "Timing Negotiation Failure");
            pw_data->pw_req.status |= PECI_WIRE_STAT_TIM_NEG_FAIL;
        }
        if (stat_reg & PECI_WIRE_BIT_COLL_FAIL)
        {
            dev_err(&pw_data->pdev->dev, "Invalid Bus Transaction");
            pw_data->pw_req.status |= PECI_WIRE_STAT_BIT_COLL_FAIL;
        }
        if (stat_reg & PECI_WIRE_FCS_RD_FAIL)
        {
            dev_err(&pw_data->pdev->dev, "Bad Read FCS");
            pw_data->pw_req.status |= PECI_WIRE_STAT_FCS_RD_FAIL;
        }
        if (stat_reg & PECI_WIRE_FCS_WT_FAIL)
        {
            dev_err(&pw_data->pdev->dev, "Bad Write FCS (possible missing client)");
            pw_data->pw_req.status |= PECI_WIRE_STAT_FCS_WT_FAIL;
        }
        if (stat_reg & PECI_WIRE_FCS_CA_FAIL)
        {
            dev_err(&pw_data->pdev->dev, "Client Aborted Transaction");
            pw_data->pw_req.status |= PECI_WIRE_STAT_FCS_CA_FAIL;
        }
        goto irq_exit;
    }

    // read response from PECIDATA
    for (i = 0; i <  pw_data->pw_req.rd_len; i++)
    {
        pw_data->pw_req.rd_buf[i] = readb(pw_data->base + PECI_WIRE_DATA + pw_data->rd_offset + i);
        if (debug_flag)
        {
            dev_info(&pw_data->pdev->dev, "Rd Buf[%d]: %02x", i, pw_data->pw_req.rd_buf[i]);
        }
    }

irq_exit:
    // Wake up the caller
    ++pw_data->response;
    wake_up(&wait_queue);
    return IRQ_HANDLED;
}

static int peci_wire_open(struct inode *inode, struct file *file)
{
    struct peci_wire_drvdata *pw_data = to_peci_wire_drvdata(file);

    if (debug_flag)
    {
        dev_info(&pw_data->pdev->dev, "open");
    }
    if (pw_data->dev_open > 0)
    {
        return -EBUSY;
    }

    ++pw_data->dev_open;
    return 0;
}

static int peci_wire_release(struct inode *inode, struct file *file)
{
    struct peci_wire_drvdata *pw_data = to_peci_wire_drvdata(file);
    if (debug_flag)
    {
        dev_info(&pw_data->pdev->dev, "close");
    }
    --pw_data->dev_open;
    return 0;
}

static const struct file_operations pw_fops = {
    .owner           = THIS_MODULE,
    .open            = peci_wire_open,
    .release         = peci_wire_release,
    .unlocked_ioctl  = peci_wire_ioctl,
};

static const struct of_device_id gxp_peci_wire_of_match[] = {
    { .compatible = "hpe,gxp-peci-wire" },
    {},
};
MODULE_DEVICE_TABLE(of, gxp_peci_wire_of_match);

static int peci_wire_probe(struct platform_device *pdev)
{
    struct peci_wire_drvdata *pw_data;
    int ret;
    int irq;
    struct resource *res;
    char *mdev_name = "peci-wire0";

    pw_data = kzalloc(sizeof(*pw_data), GFP_KERNEL);
    if (!pw_data)
        return -ENOMEM;

    platform_set_drvdata(pdev, pw_data);
    pw_data->pdev = pdev;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    pw_data->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(pw_data->base))
    {
        dev_err(&pdev->dev, "ioremap request failed");
        return PTR_ERR(pw_data->base);
    }

    // Set unique name for this device
    //   Node0 devs at 0x8000xxxx, Node1 devs at 0x8400xxxx
    pw_data->node = (res->start & 0x04000000) >> 26;
    mdev_name[strlen(mdev_name)-1] = pw_data->node + '0';

    pw_data->mdev.minor  = MISC_DYNAMIC_MINOR;
    pw_data->mdev.name   = kstrdup(mdev_name, GFP_KERNEL);
    pw_data->mdev.fops   = &pw_fops;
    pw_data->mdev.parent = NULL;
    pw_data->mdev.mode = 0660;

    ret = misc_register(&pw_data->mdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register miscdev (%s)\n", pw_data->mdev.name);
        return ret;
    }

    ret = platform_get_irq(pdev, 0);
    if (ret < 0)
    {
        dev_err(&pdev->dev, "platform_get_irq request failed");
        return ret;
    }
    irq = ret;

    ret = devm_request_irq(&pdev->dev, irq, peci_wire_irq_handler, 0, pw_data->mdev.name, pw_data);
    if (ret < 0)
    {
        dev_err(&pdev->dev, "irq request failed");
        return ret;
    }

    pw_data->dev_open = 0;
    dev_info(&pdev->dev, "HPE GXP PECI-wire (%s)", pw_data->mdev.name);

    return 0;
}

static int peci_wire_remove(struct platform_device *pdev)
{
    struct peci_wire_drvdata *pw_data = platform_get_drvdata(pdev);

    kfree(pw_data->mdev.name);
    misc_deregister(&pw_data->mdev);
    kfree(pw_data);
    if (debug_flag)
    {
        dev_info(&pw_data->pdev->dev, "unregistered");
    }

    return 0;
}

static int pw_param_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || (n != 0 && n != 1))
		return -EINVAL;

	return param_set_int(val, kp);
}

static const struct kernel_param_ops param_ops = {
	.set	= pw_param_set,
	.get	= param_get_int,
};

module_param_cb(debug, &param_ops, &debug_flag, 0664);

static struct platform_driver peci_wire_driver = {
    .probe      = peci_wire_probe,
    .remove     = peci_wire_remove,
    .driver = {
        .name = "peci-wire",
        .of_match_table = of_match_ptr(gxp_peci_wire_of_match),
    },
};

module_platform_driver(peci_wire_driver);

MODULE_AUTHOR("Brad Tanner <brad.tanner@hpe.com>");
MODULE_DESCRIPTION("HPE GXP PECI-Wire Driver");

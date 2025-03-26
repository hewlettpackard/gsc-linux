// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>

#define GXP_SPI0_MAX_CHIPSELECT		2
#define GXP_SPI_SLEEP_TIME 1
#define GXP_SPI_TIMEOUT (130 * 1000000 / GXP_SPI_SLEEP_TIME)


#define MANUAL_MODE	0
#define DIRECT_MODE	1
#define SPILDAT_LEN	256

#define OFFSET_SPIMCFG		0x0
#define OFFSET_SPIMCTRL		0x4
#define OFFSET_SPICMD			0x5
#define OFFSET_SPIDCNT		0x6
#define OFFSET_SPIADDR		0x8
#define OFFSET_SPIINTSTS	0xc

#define SPIMCTRL_START	0x01
#define SPIMCTRL_BUSY		0x02
#define SPIMCTRL_DIR		0x08

struct gxp_spifi_drvdata {
	struct device *dev;
	struct device_node *local;
	void __iomem	*reg_base;
	void __iomem	*dat_base;
	void __iomem	*dir_base;
	struct resource *res;
	u32 spi_ctrl;
	u32 reset;
	u32 current_device;
	dev_t spifidev;
	struct cdev spifi_c_dev;
	struct class *dev_cl;
	struct spi_nor nor[GXP_SPI0_MAX_CHIPSELECT];
	struct mutex mutex;
};

struct gxp_spifi_drvdata *spifictrl1;

static int gxp_spi_get_cs(struct spi_nor *nor)
{
	struct gxp_spifi_drvdata *spifi = nor->priv;
	// spi ctrl1 has only 1 CS
	if (spifi->spi_ctrl == 1)
		return 0;
	if (&spifi->nor[0] == nor)
		return 0;
	else if (&spifi->nor[1] == nor)
		return 1;
	return -1;
}

static int gxp_spi_prep(struct spi_nor *nor)
{
	struct gxp_spifi_drvdata *spifi = nor->priv;

	mutex_lock(&spifi->mutex);

	return 0;
}

static void gxp_spi_unprep(struct spi_nor *nor)
{
	struct gxp_spifi_drvdata *spifi = nor->priv;

	mutex_unlock(&spifi->mutex);
}

static void gxp_spi_set_mode(struct spi_nor *nor, int mode)
{
	u8 value;
	struct gxp_spifi_drvdata *spifi = nor->priv;
	void __iomem *reg_base = spifi->reg_base;

	value = readb(reg_base + OFFSET_SPIMCTRL);

	if (mode == MANUAL_MODE) {
		writeb(0x55, reg_base + OFFSET_SPICMD);
		writeb(0xaa, reg_base + OFFSET_SPICMD);
		value &= ~0x30;
	} else
		value |= 0x30;
	writeb(value, reg_base + OFFSET_SPIMCTRL);
}

static int gxp_spi_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, size_t len)
{
	int ret = 0;

	struct gxp_spifi_drvdata *spifi = nor->priv;
	void __iomem *reg_base = spifi->reg_base;

	u32 value;
	int cs;

	cs = gxp_spi_get_cs(nor);
	if (cs < 0) {
		//invalid nor pointer
		return -1;
	}

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1<<24);
	value |= (cs<<24);	//set chipselect
	value &= ~(0x07<<16);	//set the address size to 0
	value &= ~(0x1f<<19);	//set the dummy_cnt to 0
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(0, reg_base + OFFSET_SPIADDR);

	writeb(opcode, reg_base + OFFSET_SPICMD);

	writew(len, reg_base + OFFSET_SPIDCNT);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value &= ~SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				!(value & SPIMCTRL_BUSY),
				GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret) {
		dev_warn(spifi->dev, "read reg busy time out\n");
		return ret;
	}

	memcpy_fromio(buf, spifi->dat_base, len);
	return ret;
}

static int gxp_spi_write_reg(struct spi_nor *nor, u8 opcode, const u8 *buf, size_t len)
{
	int ret = 0;
	struct gxp_spifi_drvdata *spifi = nor->priv;
	void __iomem *reg_base = spifi->reg_base;

	u32 value;
	int cs;

	cs = gxp_spi_get_cs(nor);
	if (cs < 0) {
		//invalid nor pointer
		return -1;
	}

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1<<24);
	value |= (cs<<24);	//set chipselect
	value &= ~(0x07<<16);	//set the address size to 0
	value &= ~(0x1f<<19);	//set the dummy_cnt to 0
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(0, reg_base + OFFSET_SPIADDR);

	writeb(opcode, reg_base + OFFSET_SPICMD);

	memcpy_toio(spifi->dat_base, buf, len);

	writew(len, reg_base + OFFSET_SPIDCNT);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				!(value & SPIMCTRL_BUSY),
				GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret)
		dev_warn(spifi->dev, "write reg busy time out\n");

	return ret;
}

static ssize_t gxp_spi_read(struct spi_nor *nor, loff_t from,
			size_t len, u_char *buf)
{
	int cs;
//	int ret;
	u32 offset = from;
	struct gxp_spifi_drvdata *spifi = nor->priv;
//	const struct spi_nor_hwcaps hwcaps = {
//		.mask = SNOR_HWCAPS_READ | SNOR_HWCAPS_PP,
//	};

	cs = gxp_spi_get_cs(nor);
	if (spifi->spi_ctrl == 0) {
		if (cs < 0) {
			//invalid nor pointer
			return -1;
		} else if (cs == 0) {
			offset += 0x4000000;
		}
	} else {
		if (spifi->reset == 1) {
			spifi->reset = 2;
			// ret = spi_nor_scan(nor, NULL, &hwcaps);
			dev_info(spifi->dev, "spi-fi: Reading from ctrl1 : %x\n", len);
		}
		spifi->reset += 1;
	}

	memcpy_fromio(buf, spifi->dir_base + offset, len);

	return len;
}

static ssize_t gxp_spi_write(struct spi_nor *nor, loff_t to,
			size_t len, const u_char *buf)
{
	struct gxp_spifi_drvdata *spifi = nor->priv;
	void __iomem *reg_base = spifi->reg_base;

	u32 write_len;
	u32 value;
	int cs;
	int ret = 0;

	cs = gxp_spi_get_cs(nor);
	if (cs < 0) {
		//invalid nor pointer
		return -1;
	}

	write_len = len;
	if (write_len > SPILDAT_LEN)
		write_len = SPILDAT_LEN;

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1<<24);
	value |= (cs<<24);	//set chipselect
	value &= ~(0x07<<16);
	value |= (nor->addr_nbytes<<16); //set the address size
	value &= ~(0x1f<<19);	//set the dummy_cnt to 0
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(to, reg_base + OFFSET_SPIADDR);

	writeb(nor->program_opcode, reg_base + OFFSET_SPICMD);

	writew(write_len, reg_base + OFFSET_SPIDCNT);

	memcpy_toio(spifi->dat_base, buf, write_len);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				!(value & SPIMCTRL_BUSY),
				GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret) {
		dev_warn(spifi->dev, "write busy time out\n");
		return ret;
	}

	return write_len;
}

static int gxp_spi_erase(struct spi_nor *nor, loff_t offs)
{
	int ret = 0;
	struct gxp_spifi_drvdata *spifi = nor->priv;
	void __iomem *reg_base = spifi->reg_base;

	u32 value;
	int cs;
	if (spifi == spifictrl1)
		dev_info(spifi->dev, "Erase command\n");
	cs = gxp_spi_get_cs(nor);
	if (cs < 0) {
		//invalid nor pointer
		return -1;
	}

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1<<24);
	value |= (cs<<24);	//set chipselect
	value &= ~(0x07<<16);	//set the address size
	value |= (nor->addr_nbytes<<16);
	value &= ~(0x1f<<19);	//set the dummy_cnt to 0
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(offs, reg_base + OFFSET_SPIADDR);

	writeb(nor->erase_opcode, reg_base + OFFSET_SPICMD);
	if (spifi == spifictrl1)
		dev_info(nor->dev, "Erasing : %x\n", nor->erase_opcode);


	writew(0, reg_base + OFFSET_SPIDCNT);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				!(value & SPIMCTRL_BUSY),
				GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret)
		dev_warn(spifi->dev, "erase busy time out\n");

	return ret;
}

static const struct spi_nor_controller_ops gxp_smc_controller_ops = {
	.prepare = gxp_spi_prep,
	.unprepare = gxp_spi_unprep,
	.read_reg = gxp_spi_read_reg,
	.write_reg = gxp_spi_write_reg,
	.read = gxp_spi_read,
	.write = gxp_spi_write,
	.erase = gxp_spi_erase,
};

static int gxp_spi_setup_flash(struct gxp_spifi_drvdata *spifi,
				struct device_node *np)
{
	int ret;
	u32 spi_cs;
	struct spi_nor *nor;
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ | SNOR_HWCAPS_PP,
	};

	// On controller 1 all devices share the same CS -> 0
	// So we enumarate them based on device number
	if (spifi->spi_ctrl == 1) {
		spi_cs = spifi->current_device;
	} else if (!of_property_read_u32(np, "reg", &spi_cs)) {
		switch (spi_cs) {
		case 0:
		case 1:
			dev_info(spifi->dev, "cs reg = %d\n", spi_cs);
			break;
		default:
			dev_err(spifi->dev, "unsupported cs(%d)\n", spi_cs);
			return -EINVAL;
		}
	}

	nor = &spifi->nor[spi_cs];

	nor->dev = spifi->dev;
	spi_nor_set_flash_node(nor, np);
	nor->controller_ops = &gxp_smc_controller_ops;
	nor->priv  = spifi;

	gxp_spi_set_mode(nor, MANUAL_MODE);

	ret = spi_nor_scan(nor, NULL, &hwcaps);
	if (ret) {
		dev_err(spifi->dev, "spi flash scan failed\n");
		return ret;
	}
	dev_info(spifi->dev, "gxp-spifi: Scan done\n");
	ret = mtd_device_register(&nor->mtd, NULL, 0);
	if (ret) {
		dev_err(spifi->dev, "mtd device parse failed\n");
		return ret;
	}

	return 0;
}

static int spifi_release(struct inode *inode, struct file *flip)
{
	return 0;
}

ssize_t spifi_write(struct file *f, const char *buf, size_t len, loff_t *off)
{
	/* if (copy_to_user(buf, &drvdata->postcode, 2))
	 * return -EFAULT;
	 */
	char memory_buffer[2048];

	copy_from_user(&memory_buffer[0], buf, len);
	return 1;
}

static int spifi_open(struct inode *inode, struct file *file)
{
	unsigned short int ret;
	struct gxp_spifi_drvdata *spifi;
	struct spi_nor *nor;
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ | SNOR_HWCAPS_PP,
	};

	if (spifictrl1) {

	// Let's reset spifi ctrl1 cs0
		nor = &spifictrl1->nor[0];
		spifi = nor->priv;

		dev_info(spifi->dev, "Open Operation");
		mtd_device_unregister(&nor->mtd);
		// spi_nor_set_flash_node(nor, spifictrl1->local);
		gxp_spi_set_mode(nor, MANUAL_MODE);
		ret = spi_nor_scan(nor, NULL, &hwcaps);
		ret = mtd_device_register(&nor->mtd, NULL, 0);
	}
	return 0;
}

static const struct file_operations spifi_fops = {
	.owner		= THIS_MODULE,
	.open		= spifi_open,
	.write		= spifi_write,
	.release	= spifi_release,
};

static int gxp_spifi_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct gxp_spifi_drvdata *spifi;
	struct resource *res;
	int count = 0;
	u32 spi_ctrl;

	spifictrl1 = NULL;

	dev_info(&pdev->dev, "gxp-spifi: %s %s %d\n", pdev->dev.init_name, pdev->name,
		 pdev->num_resources);

	spifi = devm_kzalloc(&pdev->dev, sizeof(*spifi), GFP_KERNEL);
	if (!spifi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spifi->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->reg_base))
		return PTR_ERR(spifi->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	spifi->dat_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->dat_base))
		return PTR_ERR(spifi->dat_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	spifi->res = res;
	spifi->dir_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->dir_base))
		return PTR_ERR(spifi->dir_base);

	mutex_init(&spifi->mutex);

	spifi->dev = &pdev->dev;
	platform_set_drvdata(pdev, spifi);

	// which ctrl initialized ?
	if (!of_property_read_u32(pdev->dev.of_node, "ctrl", &spi_ctrl)) {
		if (spi_ctrl < 2)
			spifi->spi_ctrl = spi_ctrl;
		else
			spifi->spi_ctrl = 0;
		spifi->reset = 0;
	} else {
		spifi->spi_ctrl = 0;
	}
	spifi->current_device = 0;
	dev_info(&pdev->dev, "gxp-spifi: Current controller %d\n", spifi->spi_ctrl);
	// switch address mode to 4 byte
	writel(0x0000400F, spifi->reg_base + 0x10);

	//the spifi has two cs lines, expect two child flash device nodes.
	// First device
	np = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (np) {
		if (spifi->spi_ctrl == 1) {
			// We need to attach the SYS ROM to GXP
			// Lion specific code
			spifi->current_device = 0;
			void __iomem *SEVSTAT = ioremap(0x80200070, 4);
			unsigned long status = readl(SEVSTAT);

			dev_err(&pdev->dev, "Status registered %lx\n", status);
			iounmap(SEVSTAT);
			void __iomem *spimux = ioremap(0xD1000119, 1);
			unsigned char reg_r = readb(spimux);

			dev_err(&pdev->dev, "Current CPLD SPI allocation %x\n", reg_r);
			// Attach SPI UEFI ROM to GXP
			reg_r = reg_r & 0xEF;
			writeb(reg_r, spimux);
			reg_r = readb(spimux);
			dev_err(&pdev->dev, "New SPI Mux allocation %x\n", reg_r);
			iounmap(spimux);
		}

		if (gxp_spi_setup_flash(spifi, np))
			dev_err(&pdev->dev,
				"unable to setup flash chip at cs0\n");
		else
			count++;
	}
	// second device
	np = of_get_next_available_child(pdev->dev.of_node, np);
	if (np) {
		if (spifi->spi_ctrl == 1) {
			void __iomem *spimux = ioremap(0xD1000119, 1);
			unsigned char reg_r = readb(spimux);

			spifi->current_device = 1;
			dev_err(&pdev->dev, "Current CPLD SPI allocation %x\n", reg_r);
			// Attach SPI UEFI VAR to GXP
			reg_r = reg_r | 0x10;
			writeb(reg_r, spimux);
			reg_r = readb(spimux);
			dev_err(&pdev->dev, "New SPI Mux allocation %x\n", reg_r);
		}
		if (gxp_spi_setup_flash(spifi, np))
			dev_err(&pdev->dev,
				"unable to setup flash chip at cs1\n");
		else
			count++;
	}

	// Let's create the character device for the /input/output
	// We shall do that only if it hasn't yet be created and for ctrl1
	if (spifi->spi_ctrl == 1) {
		int ret;
		struct device *dev_ret;

		spifi->local = np;
		ret = alloc_chrdev_region(&spifi->spifidev, 0, 1, "gxp-spi");
		if (ret < 0)
			return ret;

		spifi->dev_cl = class_create("chardrvspifi");
		if (IS_ERR(spifi->dev_cl)) {
			unregister_chrdev_region(spifi->spifidev, 1);
			return PTR_ERR(spifi->dev_cl);
		}

		dev_ret = device_create(spifi->dev_cl, NULL, spifi->spifidev, NULL, "spifi");
		if (IS_ERR(dev_ret)) {
			class_destroy(spifi->dev_cl);
			unregister_chrdev_region(spifi->spifidev, 1);
			return PTR_ERR(dev_ret);
		}

		cdev_init(&spifi->spifi_c_dev, &spifi_fops);
		ret = cdev_add(&spifi->spifi_c_dev, spifi->spifidev, 1);
		if (ret < 0) {
			device_destroy(spifi->dev_cl, spifi->spifidev);
			class_destroy(spifi->dev_cl);
			unregister_chrdev_region(spifi->spifidev, 1);
			return ret;
		}

		spifictrl1 = spifi;
	}

	return count > 0 ? 0 : -ENODEV;
}

static int gxp_spifi_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id gxp_spifi_match[] = {
	{.compatible = "hpe,gxp-spifi"},
	{ /* null */ }
};
MODULE_DEVICE_TABLE(of, gxp_spifi_match);

static struct platform_driver gxp_spifi_driver = {
	.probe = gxp_spifi_probe,
	.remove = gxp_spifi_remove,
	.driver = {
		.name = "gxp-spifi",
		.of_match_table = gxp_spifi_match,
	},
};
module_platform_driver(gxp_spifi_driver);

MODULE_DESCRIPTION("HPE GXP SPI Flash Interface driver");
MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett-Packard Development Company, L.P. */

#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define GSC_SPI0_MAX_CHIPSELECT	2
#define GSC_SPI_SLEEP_TIME	5
#define GSC_SPI_TIMEOUT		(130 * 1000000)

#define SPILDAT_LEN		256

#define OFFSET_SPIMCFG		0x0
#define OFFSET_SPIMCTRL		0x4
#define OFFSET_SPICMD		0x5
#define OFFSET_SPIDCNT		0x6
#define OFFSET_SPIADDR		0x8
#define OFFSET_SPIGCFG		0x800

#define SPIMCTRL_START		0x01
#define SPIMCTRL_BUSY		0x02
#define SPIMCTRL_DIR		0x08
#define SPIMCTRL_CHAIN_EN	0x04

#define CTRL_IO_SINGLE_DATA	 0
#define CTRL_IO_DUAL_DATA	(2 << 30)
#define CTRL_IO_QUAD_DATA	(3 << 30)

#define CTRL_CMD_SINGLE_DATA	0
#define CTRL_CMD_DUAL_DATA	BIT(28)
#define CTRL_CMD_QUAD_DATA	(2 << 28)

#define CTRL_ADDR_SINGLE_DATA	0
#define CTRL_ADDR_DUAL_DATA	BIT(26)
#define CTRL_ADDR_QUAD_DATA	(2 << 26)

#define NO_FREQ_DIVISORS	9

struct gsc_spi;

struct gsc_spi_chip {
	struct gsc_spi *spifi;
	u32 cs;
	u32 frequency;
};

struct gsc_spi {
	void __iomem *reg_base;
	void __iomem *dat_base;
	void __iomem *dir_base;
	struct device *dev;
	struct gsc_spi_chip chips[GSC_SPI0_MAX_CHIPSELECT];
	u32 max_cs;
};

const int freq_divisor[] = {4, 5, 6, 7, 8, 10, 13, 16, 32};

static u32 gsc_spi_get_io_mode(const struct spi_mem_op *op)
{
	u32 ret = 0;

	switch (op->data.buswidth) {
	case 2:
		ret |= CTRL_IO_DUAL_DATA;
		break;
	case 4:
		ret |= CTRL_IO_QUAD_DATA;
		break;
	}

	switch (op->cmd.buswidth) {
	case 2:
		ret |= CTRL_CMD_DUAL_DATA;
		break;
	case 4:
		ret |= CTRL_CMD_QUAD_DATA;
		break;
	}

	switch (op->addr.buswidth) {
	case 2:
		ret |= CTRL_ADDR_DUAL_DATA;
		break;
	case 4:
		ret |= CTRL_ADDR_QUAD_DATA;
		break;
	}
	return ret;
}

static int gsc_wait_busy_signal(struct gsc_spi *spifi, const char *func_name)
{
	void __iomem *reg_base = spifi->reg_base;
	u32 value;
	int ret;

//Temporary workaround for slow spi-nor operations.
//readb_poll_timeout makes the thread sleep if-
//the condition (busy bit is not set in SPIMCTRL) is false.
//Since the arch timer is not present once this thread sleep, -
//its taking more time to get the CPU again.
//Preventing this tread from sleeping , by using busy -
//loops(till the busy bit is cleared in SPIMCTRL).
#if defined(CONFIG_ARCH_HPE_GSC_TIMER_WA)
	do {
		value = readb(reg_base + OFFSET_SPIMCTRL);
	} while (value & SPIMCTRL_BUSY);
#endif

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				 !(value & SPIMCTRL_BUSY),
				 GSC_SPI_SLEEP_TIME, GSC_SPI_TIMEOUT);
	if (ret) {
		dev_warn(spifi->dev, "busy time out on function %s\n", func_name);
	};
	return ret;
}

static void gsc_spi_set_manual_mode(struct gsc_spi *spifi)
{
	void __iomem *reg_base = spifi->reg_base;
	u8 value;

	gsc_wait_busy_signal(spifi, __func__);
	value = readb(reg_base + OFFSET_SPIMCTRL);
	writeb(0x55, reg_base + OFFSET_SPICMD);
	writeb(0xaa, reg_base + OFFSET_SPICMD);
	value &= ~(0x03 << 4);
	writeb(value, reg_base + OFFSET_SPIMCTRL);
}

static u32 gsc_get_frequency(struct gsc_spi *spifi)
{
	void __iomem *reg_base = spifi->reg_base;
	u32 value;
	u32 freq;

	value = readl(reg_base + OFFSET_SPIMCFG);
	value = value & 0x0F;

	freq = (400 / freq_divisor[value]) * 1000000;
	return freq;
}

static int gsc_frequency_divisor(u32 freq)
{
	uint divisor;
	int i;

	divisor = 400000000 / freq;
	for (i = 0; i < NO_FREQ_DIVISORS; i++) {
		if (freq_divisor[i] == divisor)
			break;
	}

	if (i >= NO_FREQ_DIVISORS)
		return 7;

	return i;
}

static void gsc_manual_cfg(struct gsc_spi *spifi, struct gsc_spi_chip *chip,
			   const struct spi_mem_op *op)
{
	void __iomem *reg_base = spifi->reg_base;
	u8 dummy_cycles = 0;
	u32 cur_value;
	u32 new_value;

	if (op->dummy.nbytes)
		dummy_cycles = op->dummy.nbytes * 8 / op->dummy.buswidth;

	cur_value = readl(reg_base + OFFSET_SPIMCFG);

	new_value = cur_value & ~(1 << 24);
	new_value |= (chip->cs << 24);
	new_value &= ~(0x07 << 16);
	new_value &= ~(0x1f << 19);
	new_value &= ~(0x3F << 26);
	new_value &= ~(0xf);
	new_value |= (op->addr.nbytes << 16);
	new_value |= (dummy_cycles << 19);
	new_value |= gsc_spi_get_io_mode(op);
	new_value |= gsc_frequency_divisor(chip->frequency);

	if (cur_value != new_value) {
		gsc_wait_busy_signal(spifi, __func__);
		writel(new_value, reg_base + OFFSET_SPIMCFG);
	}
}

static void gsc_control_start(struct gsc_spi *spifi, const struct spi_mem_op *op)
{
	void __iomem *reg_base = spifi->reg_base;
	u32 value;

	value = readb(reg_base + OFFSET_SPIMCTRL);
	if (op->data.dir == SPI_MEM_DATA_IN)
		value &= ~SPIMCTRL_DIR;
	else
		value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;
	writeb(value, reg_base + OFFSET_SPIMCTRL);
}

static int gsc_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct gsc_spi *spifi = spi_controller_get_devdata(mem->spi->controller);
	struct gsc_spi_chip *chip = &spifi->chips[spi_get_chipselect(mem->spi, 0)];
	void __iomem *reg_base = spifi->reg_base;
	uint read_len = 0;
	u32 read_ptr = 0;
	u32 addr = 0;
	int ret;

	gsc_manual_cfg(spifi, chip, op);

	writel(op->addr.val, reg_base + OFFSET_SPIADDR);

	writeb(op->cmd.opcode, reg_base + OFFSET_SPICMD);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		do {
			read_len = op->data.nbytes - read_ptr;
			if (read_len > SPILDAT_LEN)
				read_len = SPILDAT_LEN;

			writew(read_len, reg_base + OFFSET_SPIDCNT);

			gsc_control_start(spifi, op);

			ret = gsc_wait_busy_signal(spifi, __func__);
			if (ret)
				return -EINVAL;

			memcpy_fromio(op->data.buf.in + read_ptr, spifi->dat_base, read_len);
			if (op->data.nbytes == 0)
				break;

			read_ptr += read_len;

			addr = readl(reg_base + OFFSET_SPIADDR);
			addr += read_len;
			writel(addr, reg_base + OFFSET_SPIADDR);
		} while (read_ptr < op->data.nbytes);
	} else {
		writew(op->data.nbytes, reg_base + OFFSET_SPIDCNT);

		memcpy_toio(spifi->dat_base, op->data.buf.out, op->data.nbytes);

		gsc_control_start(spifi, op);

		return gsc_wait_busy_signal(spifi, __func__);
	}

	return ret;
}

static const struct spi_controller_mem_ops gsc_spi_mem_ops = {
	.exec_op = gsc_exec_mem_op,
};

static int gsc_spi_setup(struct spi_device *spi)
{
	struct gsc_spi *spifi = spi_controller_get_devdata(spi->controller);
	unsigned int cs = spi_get_chipselect(spi, 0);
	struct gsc_spi_chip *chip = &spifi->chips[cs];

	chip->spifi = spifi;
	chip->cs = cs;
	return 0;
}

static u8 gsc_check_spi_width(struct device *dev, struct device_node *flash_node)
{
	u32 tx_bus_width = 1;
	u32 rx_bus_width = 1;

	if (!of_property_read_u32(flash_node, "spi-tx-bus-width", &tx_bus_width))
		dev_info(dev, "%s: spi-tx-bus-width %d\n", flash_node->full_name, tx_bus_width);
	if (!of_property_read_u32(flash_node, "spi-rx-bus-width", &rx_bus_width))
		dev_info(dev, "%s: spi-rx-bus-width %d\n", flash_node->full_name, rx_bus_width);

	if (tx_bus_width != rx_bus_width) {
		dev_warn(dev, "Warning: %s  spi-tx-bus-width %d spi-rx-bus-width %d can't be different\n",
			 flash_node->full_name, tx_bus_width, rx_bus_width);
		return 1;
	}
	return tx_bus_width;
}

static int gsc_spifi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *spidev_node;
	struct spi_controller *ctlr;
	struct gsc_spi *spifi;
	int clk_freq;
	int chip_index;
	u32 value;
	int ret;

	ctlr = devm_spi_alloc_master(dev, sizeof(*spifi));
	if (!ctlr)
		return -ENOMEM;

	spifi = spi_controller_get_devdata(ctlr);

	platform_set_drvdata(pdev, spifi);
	spifi->dev = dev;

	spifi->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spifi->reg_base))
		return PTR_ERR(spifi->reg_base);

	spifi->dat_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(spifi->dat_base))
		return PTR_ERR(spifi->dat_base);

	spifi->dir_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(spifi->dir_base))
		return PTR_ERR(spifi->dir_base);

	ctlr->bus_num = pdev->id;
	ctlr->mem_ops = &gsc_spi_mem_ops;
	ctlr->setup = gsc_spi_setup;
	ctlr->num_chipselect = 2;
	ctlr->dev.of_node = dev->of_node;
	ctlr->mode_bits =  SPI_RX_DUAL | SPI_TX_DUAL | SPI_RX_QUAD | SPI_TX_QUAD;

	value = readl(spifi->reg_base + OFFSET_SPIMCFG);
	dev_info(dev, "SPIMCFG 0x%08x", value);

	value = readl(spifi->reg_base + OFFSET_SPIGCFG);
	dev_info(dev, "SPIGCFG 0x%08x", value);

	gsc_spi_set_manual_mode(spifi);

	ctlr->max_speed_hz = gsc_get_frequency(spifi);
	dev_info(dev, "Current frequency: %d\n", gsc_get_frequency(spifi));

	for_each_child_of_node(np, spidev_node) {
		gsc_check_spi_width(dev, spidev_node);
		if (!of_property_read_u32(spidev_node, "clock-frequency", &clk_freq))
			spifi->chips[chip_index].frequency = clk_freq;
		else
			spifi->chips[chip_index].frequency = ctlr->max_speed_hz;
		chip_index++;
	}

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register spi controller\n");
	}

	return 0;
}

static const struct of_device_id gsc_spifi_match[] = {
	{.compatible = "hpe,gsc-spifi" },
	{ /* null */ }
};
MODULE_DEVICE_TABLE(of, gsc_spifi_match);

static struct platform_driver gsc_spifi_driver = {
	.probe = gsc_spifi_probe,
	.driver = {
		.name = "gsc-spifi",
		.of_match_table = gsc_spifi_match,
	},
};
module_platform_driver(gsc_spifi_driver);

MODULE_DESCRIPTION("HPE GSC SPI Flash Interface driver");
MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_AUTHOR("Jorge Cisneros <jorge.cisneros@hpe.com>");
MODULE_LICENSE("GPL");

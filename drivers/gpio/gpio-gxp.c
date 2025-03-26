// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>

#define GPIDATL	0x40
#define GPIDATH 0x60
#define GPODATL	0xb0
#define GPODATH 0xb4
#define GPODAT2L	0xf8
#define GPODAT2H	0xfc
#define GPOOWNL	0x110
#define GPOOWNH 0x114
#define GPOOWN2L	0x118
#define GPOOWN2H	0x11c

#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1

#define INTERRUPT_BUTTON_ADDR           0xb0
#define INTERRUPT_SYSEVT_ADDR           0x70
#define INTERRUPT_SYSEVT_MASK_ADDR      0x74

#define GPIO_XREG_INTERRUPT_BASE  59
#define GPIO_SYSEVT_INTERRUPT_BASE   201

#define GPIO_CHIP_XREG    0
#define GPIO_CHIP_COMMON  1
#define GPIO_CHIP_NUM     2

enum gpio_pn {
    //GPI Byte 0
    GPI_LATCHED_INTRUSION_DETECT_STATUS = 0,
    GPI_UID_STATUS,
    GPI_UID_BLINK_STATUS,
    GPI_SW_FLEX_GPI,
    GPI_SW_NMI_GPI,
    GPI_PROCHOT_FORCE_R_ASSERTED,
    GPI_BL_eBRAKE,
    GPI_GMT_DIMM_POLLING_ENABLE,
    //GPI Byte 1
    GPI_INTRUSION_DETECT_CABLE_PRESENT,
    GPI_INTRUSION_DETECT_STATUS,
    GPI_ERROR_0,
    GPI_ERROR_1,
    GPI_ERROR_2,
    GPI_GMT_NMI,
    GPI_MEMORY_THROTTLE_ASSERTED,
    GPI_PWR_ALERT_LATCH,
    //GPI Byte 2
    GPI_BYTE2_RESERVED0,
    GPI_BYTE2_RESERVED1,
    GPI_BYTE2_RESERVED2,
    GPI_BYTE2_RESERVED3,
    GPI_BYTE2_RESERVED4,
    GPI_BYTE2_RESERVED5,
    GPI_BYTE2_RESERVED6,
    GPI_BYTE2_RESERVED7,
    //GPI Byte 3
    GPI_CATERR_PULSE_DLY_1MS,
    GPI_CATERR_DET,
    GPI_RMCA_PULSE_DLY_1MS,
    GPI_RMCA_DET,
    GPI_BYTE3_RESERVED0,
    GPI_BYTE3_RESERVED1,
    GPI_ADR_EVENT,
    GPI_ADR_COMPLETE,
    //GPI Byte 4
    GPI_PS1_INST,
    GPI_PS2_INST,
    GPI_PS3_INST,
    GPI_PS4_INST,
    GPI_PS5_INST,
    GPI_PS6_INST,
    GPI_PS7_INST,
    GPI_PS8_INST,
    //GPI Byte 5
    GPI_PS1_FAIL,
    GPI_PS2_FAIL,
    GPI_PS3_FAIL,
    GPI_PS4_FAIL,
    GPI_PS5_FAIL,
    GPI_PS6_FAIL,
    GPI_PS7_FAIL,
    GPI_PS8_FAIL,
    //GPI Byte 6
    GPI_BYTE6_RESERVED0,
    GPI_BYTE6_RESERVED1,
    GPI_BYTE6_RESERVED2,
    GPI_BYTE6_RESERVED3,
    GPI_BYTE6_RESERVED4,
    GPI_BYTE6_RESERVED5,
    GPI_PCIe2Gbx_UPPER_EN,
    GPI_PCIe2GbX_LOWER,
    //GPI Byte 7
    GPI_BYTE7_RESERVED0,
    GPI_BYTE7_RESERVED1,
    GPI_BYTE7_RESERVED2,
    GPI_BYTE7_RESERVED3,
    GPI_BYTE7_RESERVED4,
    GPI_BYTE7_RESERVED5,
    GPI_BYTE7_RESERVED6,
    GPI_BYTE7_RESERVED7,
    //
    //GPO Chain1 Bytes
    //
    //GPO Chain1 Byte0
    GPO_CH1_SW_FLEX_GPO = 64,
    GPO_CH1_PWR_BTN_MASK,
    GPO_CH1_PERST_SOURCE_SEL,
    GPO_CH1_IRC_COMPLETE,
    GPO_CH1_MEM_THROTTLE_EN,
    GPO_CH1_HSB_DISABLE,
    GPO_CH1_THROTTLE_SEL,
    GPO_CH1_HDD_OVERHEAT_NOTIFICATION,
    //GPO Chain1 Byte1
    GPO_CH1_SW_NMI_GPO,
    GPO_CH1_CLEAR_LATCHED_INTRUSION_DETECT_STATUS,
    GPO_CH1_CPU_FORCEPR_MOD_EN,
    GPO_CH1_CPU1_DISABLE,
    GPO_CH1_CPU2_DISABLE,
    GPO_CH1_CPU3_DISABLE,
    GPO_CH1_BYTE1_RESERVED0,	//GPO_CH1_VIRTUAL_SW6 as per Gen12 spec
    GPO_CH1_BYTE1_RESERVED1,
    //GPO Chain1 Byte2
    GPO_CH1_GBLRST_HOLDOFF_EN,	// GPO_CH1_BYTE2_RESERVED0 as per Gen12 spec
    GPO_CH1_CLIP_EN,
    GPO_CH1_EN_GBLRST_ON_CATERR_HOLD,
    GPO_CH1_GBLRST_CNTR_RST,
    GPO_CH1_CLIP_STAT_CLR,
    GPO_CH1_BYTE2_RESERVED0,	//GPO_CH1_ASYNC_WARM_RST as per Gen12 spec
    GPO_CH1_BYTE2_RESERVED1,
    GPO_CH1_BYTE2_RESERVED2,
    //GPO Chain1 Byte3
    GPO_CH1_FORCE_AUX_EFUSE_CYCLING,
    GPO_CH1_FORCE_AUX_EFUSE_OFF,
    GPO_CH1_FORCE_MAIN_EFUSE_OFF,
    GPO_CH1_FORCE_MAIN_EFUSE_ON,
    GPO_CH1_CPLD_RST,
    GPO_CH1_HREG_LOCK,
    GPO_CH1_UID_CLK,
    GPO_CH1_UID_BLINK,
    //GPO Chain1 Byte4
    GPO_CH1_EN_RST_ON_RMCA_PULSE,
    GPO_CH1_PSTATE_TRANSITION_REQUEST,
    GPO_CH1_CLEAR_CATERR_LOGIC,
    GPO_CH1_EN_RST_ON_CATERR_PULSE,
    GPO_CH1_DIS_RST_ON_CATERR_HOLD,
    GPO_CH1_CLEAR_RMCA_LOGIC,
    GPO_CH1_PCH_CRASHLOG_CATERR_HOLD_EN,	//GPO_CH1_BYTE4_RESERVED0 as per Gen12 spec
    GPO_CH1_PCH_CRASHLOG_GLB_RST_WARN_EN,	//GPO_CH1_BYTE4_RESERVED1 as per Gen12 spec
    //GPO Chain1 Byte5
    GPO_CH1_DDR_THERM_SW,
    GPO_CH1_DDRCAP_EN,
    GPO_CH1_FORCE_PROCESSOR_THROTTLE,
    GPO_CH1_NVME_CLK_CONTROL,
    GPO_CH1_NVME_DRIVE_I2C_PWR_DISABLE,
    GPO_CH1_BYTE5_RESERVED0,
    GPO_CH1_EBRAKE_EN,
    GPO_CH1_APPLICATION_PROCS_DISABLE_OVERRIDE,
    //GPO Chain1 Byte6
    GPO_CH1_FORCE_LEDS,
    GPO_CH1_Health_AMBER,
    GPO_CH1_HEALTH_RED,
    GPO_CH1_AMP_LED_BIT0,
    GPO_CH1_AMP_LED_BIT1,
    GPO_CH1_iLO_ACK_I2C0,
    GPO_CH1_iLO_ACK_PECI,
    GPO_CH1_OVERTEMP_LED,
    //GPO Chain1 Byte7
    GPO_CH1_ROM_DEBUG_PORT0,
    GPO_CH1_ROM_DEBUG_PORT1,
    GPO_CH1_ROM_DEBUG_PORT2,
    GPO_CH1_ROM_DEBUG_PORT3,
    GPO_CH1_ROM_DEBUG_PORT4,
    GPO_CH1_ROM_DEBUG_PORT5,
    GPO_CH1_ROM_DEBUG_PORT6,
    GPO_CH1_ROM_DEBUG_PORT7,
    //
    //GPO Chain2 Bytes
    //
    //GPO Chain2 Byte0
    GPO_CH2_EN_CPU_FPT,
    GPO_CH2_EN_MEM_FPT,
    GPO_CH2_EN_PCIE_SLOT_FPT1,
    GPO_CH2_EN_PCIE_SLOT_FPT2,
    GPO_CH2_EN_CAP_THROTTLE_SLOT_GROUP1,
    GPO_CH2_FORCE_THROTTLE_SLOT_GROUP1,
    GPO_CH2_EN_CAP_THROTTLE_SLOT_GROUP2,
    GPO_CH2_FORCE_THROTTLE_SLOT_GROUP2,
    //GPO Chain2 Byte1
    GPO_CH2_BYTE1_RESERVED0,
    GPO_CH2_BYTE1_RESERVED1,
    GPO_CH2_BYTE1_RESERVED2,
    GPO_CH2_BYTE1_RESERVED3,
    GPO_CH2_BYTE1_RESERVED4,
    GPO_CH2_BYTE1_RESERVED5,
    GPO_CH2_BYTE1_RESERVED6,
    GPO_CH2_BYTE1_RESERVED7,
    //GPO Chain2 Byte2
    GPO_CH2_THROTTLE_RSTWARN_DISABLE,
    GPO_CH2_ADR_GLB_RST_WARN_ENABLE,	//GPO_CH1_BYTE4_RESERVED1 as per Gen12 spec       
    GPO_CH2_ADR_RESET_IO_ENABLE,
    GPO_CH2_ADR_PROCHOT_ENABLE,
    GPO_CH2_ADR_ACOK_ENABLE,
    GPO_CH2_BYTE2_RESERVED,	//GPO_CH2_BYTE2_RESERVED1 as per Gen12 spec  
    GPO_CH2_ADR_POWEROFF_DELAY_ENABLE,	//GPO_CH2_BYTE2_RESERVED2 as per Gen12 spec
    GPO_CH2_BYTE2_RESERVED0_FOR_FUTURE_BACKUP_EVENTS,	//GPO_CH2_BYTE2_RESERVED3_FOR_FUTURE_BACKUP_EVENTS as per Gen12 spec
    //GPO Chain2 Byte3
    GPO_CH2_BYTE3_RESERVED0_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED1_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED2_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED3_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED4_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED5_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED6_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE3_RESERVED7_FOR_FUTURE_BACKUP_EVENTS,
    //GPO Chain2 Byte4
    GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED1_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED2_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED3_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED4_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED5_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED6_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE4_RESERVED7_FOR_FUTURE_BACKUP_EVENTS,
    //GPO Chain2 Byte5
    GPO_CH2_BYTE5_RESERVED0_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_ADR_POWER_FAULT_ENABLE,
    GPO_CH2_ADR_TRIGGER_MANUAL,
    GPO_CH2_ADR_ENABLE,
    GPO_CH2_FAST_ADR_ENABLE,
    GPO_CH2_NVDIMM_N_SAVE_MODE_ENABLE,
    GPO_CH2_BYTE5_RESERVED1_FOR_FUTURE_BACKUP_EVENTS,
    GPO_CH2_BYTE5_RESERVED2_FOR_FUTURE_BACKUP_EVENTS,
    //GPO Chain2 Byte6
    GPO_CH2_CPU0_FORCE_THROTTLE,
    GPO_CH2_CPU1_FORCE_THROTTLE,
    GPO_CH2_CPU2_FORCE_THROTTLE,
    GPO_CH2_CPU3_FORCE_THROTTLE,
    GPO_CH2_CPU4_FORCE_THROTTLE,
    GPO_CH2_CPU5_FORCE_THROTTLE,
    GPO_CH2_CPU6_FORCE_THROTTLE,
    GPO_CH2_CPU7_FORCE_THROTTLE,
    //GPO Chain2 Byte7
    GPO_CH2_BYTE7_RESERVED0_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED1_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED2_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED3_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED4_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED5_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED6_FOR_STORAGE,
    GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE,

};

struct gxp_gpio_drvdata {
	struct regmap *csm_map;
	struct gpio_chip gpio;
	int irq;
};

static int gxp_gpio_get(struct gpio_chip *gpio, unsigned int offset)
{
	struct gxp_gpio_drvdata *drvdata = dev_get_drvdata(gpio->parent);
	int ret = 0;

	switch (offset) {
	case GPI_LATCHED_INTRUSION_DETECT_STATUS ... GPI_ADR_COMPLETE:
		// GPI LOW
		regmap_read(drvdata->csm_map, GPIDATL,	&ret);
		ret = (ret & BIT(offset))?1:0;
		break;
	case GPI_PS1_INST ... GPI_BYTE7_RESERVED7:
		//GPI HIGH
		regmap_read(drvdata->csm_map, GPIDATH,	&ret);
		ret = (ret & BIT(offset - GPI_PS1_INST))?1:0;
		break;
	case GPO_CH1_SW_FLEX_GPO ... GPO_CH1_UID_BLINK:
		//GPO chain 1 LOW
		regmap_read(drvdata->csm_map, GPODATL,	&ret);
		ret = (ret & BIT(offset - GPO_CH1_SW_FLEX_GPO))?1:0;
		break;
	case GPO_CH1_EN_RST_ON_RMCA_PULSE ... GPO_CH1_ROM_DEBUG_PORT7:
		//GPO chain 1 HIGH
		regmap_read(drvdata->csm_map, GPODATH,	&ret);
		ret = (ret & BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE))?1:0;
		break;
	case GPO_CH2_EN_CPU_FPT ...  GPO_CH2_BYTE3_RESERVED7_FOR_FUTURE_BACKUP_EVENTS:
		//GPO chain 2 LOW
		regmap_read(drvdata->csm_map, GPODAT2L,	&ret);
		ret = (ret & BIT(offset - GPO_CH2_EN_CPU_FPT))?1:0;
		break;
	case GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS ... GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE:
		//GPO chain 2 HIGH
		regmap_read(drvdata->csm_map, GPODAT2H,	&ret);
		ret = (ret & BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS))?1:0;
		break;
	case 192:
		//SW_RESET
		regmap_read(drvdata->csm_map, 0x5C,	&ret);
		ret = (ret & BIT(15))?1:0;
		break;
	default:
		break;
	}
	return ret;
}

static void gxp_gpio_set(struct gpio_chip *gpio, unsigned int offset,
		int value)
{
	struct gxp_gpio_drvdata *drvdata = dev_get_drvdata(gpio->parent);
	uint32_t tmp;

	switch (offset) {
	case GPO_CH1_SW_FLEX_GPO ... GPO_CH1_UID_BLINK:
		//GPO chain 1 LOW
		//keep onwership setting
		regmap_read(drvdata->csm_map, GPOOWNL, &tmp);
		tmp = (tmp&BIT(offset - GPO_CH1_SW_FLEX_GPO))?1:0;

		//output value
		regmap_update_bits(drvdata->csm_map, GPOOWNL,
				BIT(offset - GPO_CH1_SW_FLEX_GPO), BIT(offset - GPO_CH1_SW_FLEX_GPO));
		regmap_update_bits(drvdata->csm_map, GPODATL,
				BIT(offset - GPO_CH1_SW_FLEX_GPO), value?BIT(offset - GPO_CH1_SW_FLEX_GPO):0);

		//restore ownership setting
		regmap_update_bits(drvdata->csm_map, GPOOWNL,
				BIT(offset - GPO_CH1_SW_FLEX_GPO), tmp?BIT(offset - GPO_CH1_SW_FLEX_GPO):0);
		break;
	case GPO_CH1_EN_RST_ON_RMCA_PULSE ... GPO_CH1_ROM_DEBUG_PORT7:
		//GPO chain 1 HIGH
		//keep onwership setting
		regmap_read(drvdata->csm_map, GPOOWNH, &tmp);
		tmp = (tmp&BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE))?1:0;

		//output value
		regmap_update_bits(drvdata->csm_map, GPOOWNH,
				BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE),	BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE));
		regmap_update_bits(drvdata->csm_map, GPODATH,
				BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE), value?BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE):0);

		//restore ownership setting
		regmap_update_bits(drvdata->csm_map, GPOOWNH,
				BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE), tmp?BIT(offset - GPO_CH1_EN_RST_ON_RMCA_PULSE):0);
		break;
	case GPO_CH2_EN_CPU_FPT ... GPO_CH2_BYTE3_RESERVED7_FOR_FUTURE_BACKUP_EVENTS:
		//GPO chain 2 LOW
		//keep onwership setting
		regmap_read(drvdata->csm_map, GPOOWN2L, &tmp);
		tmp = (tmp&BIT(offset - GPO_CH2_EN_CPU_FPT))?1:0;

		//output value
		regmap_update_bits(drvdata->csm_map, GPOOWN2L,
				BIT(offset - GPO_CH2_EN_CPU_FPT), BIT(offset - GPO_CH2_EN_CPU_FPT));
		regmap_update_bits(drvdata->csm_map, GPODAT2L,
				BIT(offset - GPO_CH2_EN_CPU_FPT), value?BIT(offset - GPO_CH2_EN_CPU_FPT):0);

		//restore ownership setting
		regmap_update_bits(drvdata->csm_map, GPOOWN2L,
				BIT(offset - GPO_CH2_EN_CPU_FPT), tmp?BIT(offset - GPO_CH2_EN_CPU_FPT):0);
		break;
	case GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS ... GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE:
		//GPO chain 2 HIGH
		//keep onwership setting
		regmap_read(drvdata->csm_map, GPOOWN2H,	&tmp);
		tmp = (tmp&BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS))?1:0;

		//output value
		regmap_update_bits(drvdata->csm_map, GPOOWN2H,
				BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS), BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS));
		regmap_update_bits(drvdata->csm_map, GPODAT2H,
				BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS), value?BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS):0);

		//restore ownership setting
		regmap_update_bits(drvdata->csm_map, GPOOWN2H,
				BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS), tmp?BIT(offset - GPO_CH2_BYTE4_RESERVED0_FOR_FUTURE_BACKUP_EVENTS):0);
		break;
	case 192:
		//SW_RESET
		if (value) {
			regmap_update_bits(drvdata->csm_map, 0x5C,
					BIT(0), BIT(0)); //unmask
			regmap_update_bits(drvdata->csm_map, 0x5C,
					BIT(15), BIT(15));
		} else {
			regmap_update_bits(drvdata->csm_map, 0x5C,
					BIT(15), 0);
		}
		break;
	default:
		break;
	}
}

static int gxp_gpio_get_direction(struct gpio_chip *gpio,
		unsigned int offset)
{
	int ret = 0;

	switch (offset) {
	case GPI_LATCHED_INTRUSION_DETECT_STATUS ... GPI_BYTE7_RESERVED7:
		ret = GPIO_DIR_IN;
		break;
	case GPO_CH1_SW_FLEX_GPO ... GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE:
		ret = GPIO_DIR_OUT;
		break;
	case 192:
		ret = GPIO_DIR_OUT;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_gpio_direction_input(struct gpio_chip *gpio,
		unsigned int offset)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case GPI_LATCHED_INTRUSION_DETECT_STATUS ... GPI_BYTE7_RESERVED7:
    case GPO_CH1_SW_FLEX_GPO ... GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE:
		ret = 0;
		break;
	case 192:
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int gxp_gpio_direction_output(struct gpio_chip *gpio,
		unsigned int offset, int value)
{
	int ret = -ENOTSUPP;

	switch (offset) {
	case GPO_CH1_SW_FLEX_GPO ... GPO_CH2_BYTE7_RESERVED7_FOR_STORAGE:
	case 192:
		gxp_gpio_set(gpio, offset, value);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}


static const struct gpio_chip common_chip = {
	.label			= "gxp_gpio",
	.owner			= THIS_MODULE,
	.get			= gxp_gpio_get,
	.set			= gxp_gpio_set,
	.get_direction = gxp_gpio_get_direction,
	.direction_input = gxp_gpio_direction_input,
	.direction_output = gxp_gpio_direction_output,
	.base = -1,
	//.can_sleep		= true,
};

static int gxp_gpio_probe(struct platform_device *pdev)
{
	int ret;
	struct gxp_gpio_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct device *parent;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent\n");
		return -ENODEV;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_gpio_drvdata),
				GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	drvdata->csm_map = syscon_regmap_lookup_by_phandle(dev->of_node,
							"csm_handle");
	if (IS_ERR(drvdata->csm_map)) {
		dev_err(dev, "failed to map csm_handle\n");
		return -ENODEV;
	}

	drvdata->gpio = common_chip;
	drvdata->gpio.ngpio = 193;
					// 0~63: csm GPI
					// 64~127: csm GPO chain1
					// 128~191: csm GPO chain2
					// 192: csm misc.
	drvdata->gpio.parent = &pdev->dev;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio, NULL);
	if (ret < 0)
		dev_err(&pdev->dev, "could not register gpiochip, %d\n", ret);

	return 0;
}

static const struct of_device_id gxp_gpio_of_match[] = {
	{ .compatible = "hpe,gxp-gpio"},
	{}
};
MODULE_DEVICE_TABLE(of, gxp_gpio_of_match);

static struct platform_driver gxp_gpio_driver = {
	.driver = {
		.name	= "gxp-gpio",
		.of_match_table = gxp_gpio_of_match,
	},
	.probe		= gxp_gpio_probe,
};
module_platform_driver(gxp_gpio_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("GPIO interface for GXP");

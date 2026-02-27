// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Hewlett Packard Enterprise Development LP */

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/of.h>
#include "linux/gxp-soclib.h"

#define VXROMCFG_REG_ADDR	(0x801f00a0)
#define SMEMCFG_REG_ADDR	(0x80200050)
#define EXROMCFG_REG_ADDR	(0x80200054)
#define AVRCFG_REG_ADDR		(0x80200058)
#define SMSECATTR_REG_ADDR	(0x802000c8)
#define MSMEMCFG_REG_ADDR	(0x80fd0550)
#define MSMSECATTR_REG_ADDR	(0x80fd05c8)
#define VROMOFF_REG_ADDR	(0x80fc00f4)

#define  VALUE_CHECK_NO			(0u)
#define  VALUE_CHECK_INVALID_VALUE	(1u)

struct sysfs_dev_file_data_t {
	char dev_file_name[15u];
	unsigned long physicalAddr;
	void __iomem *virtualAddr;
	unsigned char type_of_value_check;
	unsigned char size;
	struct mutex mutex;
};

static struct sysfs_dev_file_data_t local_sysfs_file_info[] = {
#ifndef CONFIG_HPE_GXP_RELEASE
		{ "vxromcfg", 0x801f00a0, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "avrcfg", 0x80200058, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "cawbase", 0xc0000170, NULL, VALUE_CHECK_NO, 4u, },
#endif
		{ "hostcmd", 0x80200040, NULL, VALUE_CHECK_NO, 4u, },
		{ "smemcfg", 0x80200050, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "exromcfg", 0x80200054, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "uacfg", 0xc00000af, NULL, VALUE_CHECK_NO, 1u, },
		{ "vromoff", 0x80fc00f4, NULL, VALUE_CHECK_NO, 4u, },
#ifdef CONFIG_ARCH_HPE_GXP
		{ "iopccr", 0xc0000000, NULL, VALUE_CHECK_NO, 4u, },
		{ "apuccr", 0xc0000100, NULL, VALUE_CHECK_NO, 4u, },
		{ "snasmemcfg", 0x807000a4, NULL, VALUE_CHECK_NO, 4u, },
		{ "testtri", 0xc00000ac, NULL, VALUE_CHECK_NO, 2u, },
		{ "psec", 0xc00000bc, NULL, VALUE_CHECK_NO, 4u, },
		{ "avscfg1", 0xc0000138, NULL, VALUE_CHECK_NO, 4u, },
		{ "avsctrl", 0xc000013c, NULL, VALUE_CHECK_NO, 2u, },
		{ "avscfg2", 0xc000013e, NULL, VALUE_CHECK_NO, 2u, },
		{ "intcpenppuiop", 0xceff0020, NULL, VALUE_CHECK_NO, 4u, },
		{ "intcpenppuh0", 0x80f00020, NULL, VALUE_CHECK_NO, 4u, },
		{ "intcpenppuh1", 0x84f00020, NULL, VALUE_CHECK_NO, 4u, },
		{ "intcpenapuiop", 0xcefff020, NULL, VALUE_CHECK_NO, 4u, },
		{ "intcpenapuh0", 0x80f0f020, NULL, VALUE_CHECK_NO, 4u, },
		{ "intcpenapuh1", 0x84f0f020, NULL, VALUE_CHECK_NO, 4u, },
#endif
#ifdef CONFIG_ARCH_HPE_GSC
#ifndef CONFIG_HPE_GXP_RELEASE
		{ "smsecidx", 0x802000c0, NULL, VALUE_CHECK_NO, 4u, },
		{ "smsecaddr", 0x802000c4, NULL, VALUE_CHECK_NO, 4u, },
		{ "smsecattr", 0x802000c8, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "msmemcfg", 0x80fd0550, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "mmbidbo", 0x80fd0554, NULL, VALUE_CHECK_NO, 4u, },
		{ "msmsecidx", 0x80fd05c0, NULL, VALUE_CHECK_NO, 4u, },
		{ "msmsecaddr", 0x80fd05c4, NULL, VALUE_CHECK_NO, 4u, },
		{ "msmsecattr", 0x80fd05c8, NULL, VALUE_CHECK_INVALID_VALUE, 4u, },
		{ "iopdbgctll", 0xc0001bd0, NULL, VALUE_CHECK_NO, 4u, },
		{ "iopdbgctlh", 0xc0001bd4, NULL, VALUE_CHECK_NO, 4u, },
		{ "iopdbgcntrl0", 0xc0001bc0, NULL, VALUE_CHECK_NO, 4u, },
		{ "iopdbgcntrl1", 0xc0001bc8, NULL, VALUE_CHECK_NO, 4u, },
		{ "iopdbgcntrh0", 0xc0001bc4, NULL, VALUE_CHECK_NO, 4u, },
		{ "iopdbgcntrh1", 0xc0001bcc, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcjto", 0x80fe320c, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcjc", 0x80fe3210, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcfac", 0x80fe3300, NULL, VALUE_CHECK_NO, 4u, },
#endif
		{ "ethmiicfg", 0xc00000a8, NULL, VALUE_CHECK_NO, 1u, },
		{ "espigcfg", 0x80fe1000, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr0", 0x80fe3000, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr1", 0x80fe3004, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr2", 0x80fe3008, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr3", 0x80fe300c, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr4", 0x80fe3010, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr5", 0x80fe3014, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr6", 0x80fe3018, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcprr7", 0x80fe301c, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap0", 0x80fe3100, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap1", 0x80fe3120, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap2", 0x80fe3140, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap3", 0x80fe3160, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap4", 0x80fe3180, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap5", 0x80fe31a0, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap6", 0x80fe31c0, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcrap7", 0x80fe31e0, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifclap", 0x80fe31fc, NULL, VALUE_CHECK_NO, 4u,  },
		{ "espifcjbar", 0x80fe3200, NULL, VALUE_CHECK_NO, 4u, },
		{ "espifcjsz", 0x80fe3204, NULL, VALUE_CHECK_NO, 4u,  },
		{ "espifcjcnt", 0x80fe3208, NULL, VALUE_CHECK_NO, 4u, },
#endif
	};

#define NUMBER_OF_REG_LIST	(sizeof(local_sysfs_file_info)/sizeof(struct sysfs_dev_file_data_t))

struct gxp_kw_reg_drvdata {
	struct platform_device *pdev;
	struct device *dev;
	struct sysfs_dev_file_data_t sysfs_dev_file_data[NUMBER_OF_REG_LIST];
};

static ssize_t common_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int value=0u;
	ssize_t ret = ((ssize_t)-1);
	unsigned char index=0;
	struct gxp_kw_reg_drvdata *drvdata;
	drvdata = dev_get_drvdata(dev);
	
	if (drvdata == NULL) {
                pr_err("%s: gxp-kw-reg : Failed to get driver data \n", __func__);
		return ((ssize_t)-1);
	}
	
	while (index < NUMBER_OF_REG_LIST) {
		if (!strcmp((attr->attr).name, drvdata->sysfs_dev_file_data[index].dev_file_name))
			break;

		index++;
	}

	if (index == NUMBER_OF_REG_LIST) {
		// did not find entry , return fail
		ret  = ((ssize_t)-1);
	} else  {
		mutex_lock(&(drvdata->sysfs_dev_file_data[index].mutex));
		if (drvdata->sysfs_dev_file_data[index].size == 1u) {
			value = readb(drvdata->sysfs_dev_file_data[index].virtualAddr);
			ret = sprintf(buf, "0x%02x", value);
		} else if (drvdata->sysfs_dev_file_data[index].size == 2u) {
			value = readw(drvdata->sysfs_dev_file_data[index].virtualAddr);
			ret = sprintf(buf, "0x%04x", value);
		} else {
			value = readl(drvdata->sysfs_dev_file_data[index].virtualAddr);
			ret = sprintf(buf, "0x%08x", value);
		}
		mutex_unlock(&(drvdata->sysfs_dev_file_data[index].mutex));
	}
	
	return ret;
}

static unsigned char check_reg_value(unsigned int value, unsigned long physicalAddr)
{
	unsigned char ret = 0u;
	
	if (physicalAddr == VXROMCFG_REG_ADDR) {
		if ((value & 0b1111) < 0b1101)
			ret = 1u;
	} else if (physicalAddr == SMEMCFG_REG_ADDR) {
	#ifdef CONFIG_ARCH_HPE_GXP
		if ((value & 0b1111) < 0b1110)
	#endif
		{
			ret = 1u;
		}
	} else if (physicalAddr == EXROMCFG_REG_ADDR) {
		if ((value & 0b1111) < 0b1101)
			ret = 1u;
	} else if (physicalAddr == AVRCFG_REG_ADDR) {
		if ((value & 0b1111) < 0b1101)
			ret = 1u;
	}
#ifdef CONFIG_ARCH_HPE_GSC
	else if (physicalAddr == SMSECATTR_REG_ADDR) {
		if ((((value >> 24u) & 0b11) != 0b11) &&
		    ((((value >> 16u) & 0b1111) > 0b0100)) &&
		    ((((value >> 16u) & 0b1111) != 0b1100))) {
			ret = 1u;
		}
	} else if (physicalAddr == MSMEMCFG_REG_ADDR) {
		if ((value & 0b1111) < 0b1000)
			ret = 1u;
	} else if (physicalAddr == MSMSECATTR_REG_ADDR) {
		if ((((value >> 24u) & 0b11) != 0b11))
			ret = 1u;
	}
#endif
	
	return ret;
}

static ssize_t common_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct gxp_kw_reg_drvdata  *drvdata;
	unsigned int value=0u;
	int rc;
	unsigned char index=0u;
	unsigned char val_check_ret = 0u;

	drvdata = dev_get_drvdata(dev);
	if (drvdata == NULL) {
		pr_err("%s: gxp-kw-reg : Failed to get driver data\n", __func__);
		return (-EINVAL);
	}

	while (index < NUMBER_OF_REG_LIST) {
		if (!strcmp((attr->attr).name, drvdata->sysfs_dev_file_data[index].dev_file_name))
			break;
		index++;
	}

	if (index == NUMBER_OF_REG_LIST) {
		count = (-EINVAL);
	} else {
		rc = kstrtouint(buf, 0, &value);
		if (rc < 0) {
			pr_err("%s: gxp-kw-reg : Failed to write invalid value to %s : value is not a number\n",
			__func__, (drvdata->sysfs_dev_file_data[index].dev_file_name));
			return (-EINVAL);
		}

		if ((drvdata->sysfs_dev_file_data[index].type_of_value_check) ==
		    VALUE_CHECK_INVALID_VALUE) {
			val_check_ret =
				check_reg_value(value,
						drvdata->sysfs_dev_file_data[index].physicalAddr);
			if (!val_check_ret) {
				pr_err("%s: gxp-kw-reg : Failed to write unsupported value to %s\n",
				       __func__,
				       (drvdata->sysfs_dev_file_data[index].dev_file_name));
				return (-EINVAL);
			}
		}

		mutex_lock(&(drvdata->sysfs_dev_file_data[index].mutex));

		if (drvdata->sysfs_dev_file_data[index].size == 1u)
			writeb((u8)value, drvdata->sysfs_dev_file_data[index].virtualAddr);
		else if (drvdata->sysfs_dev_file_data[index].size == 2u)
			writew((u16)value, drvdata->sysfs_dev_file_data[index].virtualAddr);
		else
			writel(value, drvdata->sysfs_dev_file_data[index].virtualAddr);

		mutex_unlock(&(drvdata->sysfs_dev_file_data[index].mutex));
	}
	return count;
}

#ifndef CONFIG_HPE_GXP_RELEASE
static DEVICE_ATTR(vxromcfg, 0644, common_show, common_store);
static DEVICE_ATTR(avrcfg, 0644, common_show, common_store);
static DEVICE_ATTR(cawbase, 0644, common_show, common_store);
#endif
static DEVICE_ATTR(hostcmd, 0644, common_show, common_store);
static DEVICE_ATTR(smemcfg, 0644, common_show, common_store);
static DEVICE_ATTR(exromcfg, 0644, common_show, common_store);
static DEVICE_ATTR(vromoff, 0644, common_show, common_store);
static DEVICE_ATTR(uacfg, 0644, common_show, common_store);
#ifdef CONFIG_ARCH_HPE_GXP
static DEVICE_ATTR(iopccr, 0644, common_show, common_store);
static DEVICE_ATTR(testtri, 0644, common_show, common_store);
static DEVICE_ATTR(apuccr, 0644, common_show, common_store);
static DEVICE_ATTR(snasmemcfg, 0644, common_show, common_store);
static DEVICE_ATTR(psec, 0644, common_show, common_store);
static DEVICE_ATTR(avscfg1, 0644, common_show, common_store);
static DEVICE_ATTR(avsctrl, 0644, common_show, common_store);
static DEVICE_ATTR(avscfg2, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenppuiop, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenppuh0, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenppuh1, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenapuiop, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenapuh0, 0644, common_show, common_store);
static DEVICE_ATTR(intcpenapuh1, 0644, common_show, common_store);
#endif
#ifdef CONFIG_ARCH_HPE_GSC
#ifndef CONFIG_HPE_GXP_RELEASE
static DEVICE_ATTR(smsecidx, 0644, common_show, common_store);
static DEVICE_ATTR(smsecaddr, 0644, common_show, common_store);
static DEVICE_ATTR(smsecattr, 0644, common_show, common_store);
static DEVICE_ATTR(msmemcfg, 0644, common_show, common_store);
static DEVICE_ATTR(mmbidbo, 0644, common_show, common_store);
static DEVICE_ATTR(msmsecidx, 0644, common_show, common_store);
static DEVICE_ATTR(msmsecaddr, 0644, common_show, common_store);
static DEVICE_ATTR(msmsecattr, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgctll, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgctlh, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgcntrl0, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgcntrl1, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgcntrh0, 0644, common_show, common_store);
static DEVICE_ATTR(iopdbgcntrh1, 0644, common_show, common_store);
static DEVICE_ATTR(espifcjto, 0644, common_show, common_store);
static DEVICE_ATTR(espifcjc, 0644, common_show, common_store);
static DEVICE_ATTR(espifcfac, 0644, common_show, common_store);
#endif
static DEVICE_ATTR(ethmiicfg, 0644, common_show, common_store);
static DEVICE_ATTR(espigcfg, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr0, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr1, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr2, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr3, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr4, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr5, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr6, 0644, common_show, common_store);
static DEVICE_ATTR(espifcprr7, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap0, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap1, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap2, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap3, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap4, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap5, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap6, 0644, common_show, common_store);
static DEVICE_ATTR(espifcrap7, 0644, common_show, common_store);
static DEVICE_ATTR(espifclap,  0644, common_show, common_store);
static DEVICE_ATTR(espifcjbar, 0644, common_show, common_store);
static DEVICE_ATTR(espifcjsz,  0644, common_show, common_store);
static DEVICE_ATTR(espifcjcnt, 0644, common_show, common_store);
#endif

static struct attribute *kw_reg_list_attrs[] = {
#ifndef CONFIG_HPE_GXP_RELEASE
	&dev_attr_vxromcfg.attr,
	&dev_attr_avrcfg.attr,
	&dev_attr_cawbase.attr,
#endif
	&dev_attr_hostcmd.attr,
	&dev_attr_smemcfg.attr,
	&dev_attr_exromcfg.attr,
	&dev_attr_vromoff.attr,
	&dev_attr_uacfg.attr,
#ifdef CONFIG_ARCH_HPE_GXP
	&dev_attr_iopccr.attr,
	&dev_attr_testtri.attr,
	&dev_attr_apuccr.attr,
	&dev_attr_snasmemcfg.attr,
	&dev_attr_psec.attr,
	&dev_attr_avscfg1.attr,
	&dev_attr_avsctrl.attr,
	&dev_attr_avscfg2.attr,
	&dev_attr_intcpenppuiop.attr,
	&dev_attr_intcpenppuh0.attr,
	&dev_attr_intcpenppuh1.attr,
	&dev_attr_intcpenapuiop.attr,
	&dev_attr_intcpenapuh0.attr,
	&dev_attr_intcpenapuh1.attr,
#endif	
#ifdef CONFIG_ARCH_HPE_GSC
#ifndef CONFIG_HPE_GXP_RELEASE
	&dev_attr_smsecidx.attr,
	&dev_attr_smsecaddr.attr,
	&dev_attr_smsecattr.attr,
	&dev_attr_msmemcfg.attr,
	&dev_attr_mmbidbo.attr,
	&dev_attr_msmsecidx.attr,
	&dev_attr_msmsecaddr.attr,
	&dev_attr_msmsecattr.attr,
	&dev_attr_iopdbgctll.attr,
	&dev_attr_iopdbgctlh.attr,
	&dev_attr_iopdbgcntrl0.attr,
	&dev_attr_iopdbgcntrl1.attr,
	&dev_attr_iopdbgcntrh0.attr,
	&dev_attr_iopdbgcntrh1.attr,
	&dev_attr_espifcjto.attr,
	&dev_attr_espifcjc.attr,
	&dev_attr_espifcfac.attr,
#endif
	&dev_attr_ethmiicfg.attr,
	&dev_attr_espigcfg.attr,
	&dev_attr_espifcprr0.attr,
	&dev_attr_espifcprr1.attr,
	&dev_attr_espifcprr2.attr,
	&dev_attr_espifcprr3.attr,
	&dev_attr_espifcprr4.attr,
	&dev_attr_espifcprr5.attr,
	&dev_attr_espifcprr6.attr,
	&dev_attr_espifcprr7.attr,
	&dev_attr_espifcrap0.attr,
	&dev_attr_espifcrap1.attr,
	&dev_attr_espifcrap2.attr,
	&dev_attr_espifcrap3.attr,
	&dev_attr_espifcrap4.attr,
	&dev_attr_espifcrap5.attr,
	&dev_attr_espifcrap6.attr,
	&dev_attr_espifcrap7.attr,
	&dev_attr_espifclap.attr,
	&dev_attr_espifcjbar.attr,
	&dev_attr_espifcjsz.attr,
	&dev_attr_espifcjcnt.attr,
#endif
	NULL,
};

ATTRIBUTE_GROUPS(kw_reg_list);

static int sysfs_register(struct device *parent, struct gxp_kw_reg_drvdata *drvdata)
{
	struct device *dev;
	dev = device_create_with_groups(soc_class, parent, 0, drvdata, kw_reg_list_groups,
					"kw_reg_list");
	if (IS_ERR(dev)) {
		pr_err("%s: gxp-kw-reg : failed to create sysfs interfaces\n", __func__);
		return PTR_ERR(dev);
	}
	drvdata->dev = dev;
	return 0;
}

static int define_dram_regions(struct platform_device *pdev)
{
	void __iomem *vAddr;
	u32 mask, config[2], val;
	struct device_node *node = pdev->dev.of_node;

	if (!node) {
		dev_err(&pdev->dev, "could not find device info\n");
		return -EINVAL;
	}

	/* AVRAM region */
	if (!of_property_read_u32_array(node, "avrcfg_reg_val", config, 2)) {
		if (config[0] & 0xfff) {  //validate base is 4k aligned or not
			pr_err("%s: gxp-kw-reg : Failed to write unsupported value to avrcfg register\n", __func__);
			return (-EINVAL);
		}

		if (!check_reg_value(config[1], AVRCFG_REG_ADDR)) {  //size validation
			pr_err("%s: gxp-kw-reg : Failed to write unsupported value to avrcfg register\n", __func__);
			return (-EINVAL);
		}

		vAddr = ioremap(AVRCFG_REG_ADDR, 4u);
		writel(config[0] | config[1], vAddr);
	}

	/* VROM(SROM RAM) region */
	if (!of_property_read_u32_array(node, "vromoff_reg_val", config, 2)) {
		switch (config[1]) {
			case 1:
				mask = 0x1FFFFFF; //32MB
				break;
			case 2:
				mask = 0x3FFFFFF; //64MB
				break;
			case 3:
				mask = 0xFFFFF;   //1MB
				break;
			case 4:
				mask = 0x1FFFFF;  //2MB
				break;
			case 5:
				mask = 0x3FFFFF;  //4MB
				break;
			case 6:
				mask = 0x7FFFFF;  //8MB
				break;
			case 7:
				mask = 0xFFFFFF;  //16MB
				break;
			default:
				pr_err("%s: gxp-kw-reg : Failed to write unsupported value to vromoff register\n", __func__);
				return (-EINVAL);
		}
		if(config[0] & mask) {  //validate base is aligned or not
			pr_err("%s: gxp-kw-reg : Failed to write unsupported value to vromoff register\n", __func__);
			return (-EINVAL);
		}

		config[0] = config[0] | (1 << 7); //Enable SROM RAM Write Protect
		vAddr = ioremap(VROMOFF_REG_ADDR, 4u);
		val = readl(vAddr);
		// if VROM is disabled, then program the offset and size in the register
		if ((val & 0x08) == 0x00)
			writel(config[0] | config[1], vAddr);
	}

	return 0;
}

static int gxp_kw_reg_probe(struct platform_device *pdev)
{
	struct gxp_kw_reg_drvdata *drvdata;
	unsigned char idx = 0u;

	pr_info("gxp-kw-reg driver probe: number of kw registers = %zu\n", NUMBER_OF_REG_LIST);
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gxp_kw_reg_drvdata),GFP_KERNEL);
	if (!drvdata) {
		pr_err("%s: gxp-kw-reg : kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	drvdata->pdev = pdev;
	while (idx < NUMBER_OF_REG_LIST) {
		strcpy(drvdata->sysfs_dev_file_data[idx].dev_file_name,
		       local_sysfs_file_info[idx].dev_file_name);
		drvdata->sysfs_dev_file_data[idx].physicalAddr =
					local_sysfs_file_info[idx].physicalAddr;
		drvdata->sysfs_dev_file_data[idx].type_of_value_check =
					local_sysfs_file_info[idx].type_of_value_check;
		drvdata->sysfs_dev_file_data[idx].size =
					local_sysfs_file_info[idx].size;
		drvdata->sysfs_dev_file_data[idx].virtualAddr =
					ioremap(drvdata->sysfs_dev_file_data[idx].physicalAddr,
						drvdata->sysfs_dev_file_data[idx].size);
		mutex_init(&(drvdata->sysfs_dev_file_data[idx].mutex));
		if (!(drvdata->sysfs_dev_file_data[idx].virtualAddr)) {
			pr_err("Failed to remap physical address");
			return -ENOMEM;
		}
		idx++;
	}
	platform_set_drvdata(pdev, drvdata);
	sysfs_register(&pdev->dev, drvdata);
	return define_dram_regions(pdev);
}

static void sysfs_kw_reg_remove(struct platform_device *pdev)
{
	unsigned char index=0;
	struct gxp_kw_reg_drvdata  *drvdata;

	pr_info("%s: device removed\n", __func__);
	drvdata = dev_get_drvdata(&(pdev->dev));
	if (drvdata != NULL) {
		while (index < NUMBER_OF_REG_LIST) {
			iounmap(drvdata->sysfs_dev_file_data[index].virtualAddr);	
			index++;
		}

		devm_kfree(&(pdev->dev),drvdata);
		drvdata=NULL;
	}
}

static const struct of_device_id gxp_kw_reg_of_match[] = {
	{ .compatible = "hpe,gxp-kw-reg" },
	{},
};

MODULE_DEVICE_TABLE(of, gxp_kw_reg_of_match);

static struct platform_driver gxp_kw_reg_driver = {
	.probe = gxp_kw_reg_probe,
	.remove = sysfs_kw_reg_remove,
	.driver = {
		.name = "gxp-kw-reg",
		.of_match_table = of_match_ptr(gxp_kw_reg_of_match),
	},
};
module_platform_driver(gxp_kw_reg_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arun S Nair");
MODULE_DESCRIPTION("HPE GXP sysfs driver to read/write kernel write only registers from userspace");

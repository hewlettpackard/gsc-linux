// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2024 Hewlett Packard Enterprise Development LP */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>

#define READ_REG_CMD		0x00
#define READ_FRU_CMD		0x22
#define REG_IN_VOL		0x10
#define REG_IN_CUR		0x11
#define REG_IN_PWR		0x12
#define REG_OUT_VOL		0x20
#define REG_OUT_CUR		0x21
#define REG_OUT_PWR		0x22
#define REG_FAN_SPEED		0x40
#define REG_INLET_TEMP		0x42
#define MAX_PSU			0x08

struct eeprom_data {
	struct bin_attribute bin;
	spinlock_t buffer_lock; /* lock the buffer */
	u16 buffer_idx;
	u16 address_mask;
	u8 num_address_bytes;
	u8 idx_write_cnt;
	bool read_only;
	u8 buffer[];
};

struct gxp_psu_drvdata {
	struct i2c_client *client;
	u16 input_power;
	u16 input_voltage;
	u16 input_current;
	u16 output_power;
	u16 output_voltage;
	u16 output_current;
	s16 inlet_temp;
	u16 fan_speed;
	u8 spare_part[10];
	u8 product_name[26];
	u8 serial_number[14];
	u8 product_manufacturer[3];
	bool present; // in some cases the PSU might have been pulled
	struct mutex update_lock;
	struct device *hwmon_dev;
	// We can expose the FRU eeprom through a slave eeprom
	struct eeprom_data *eeprom;
};

struct mutex psu_drv_lock; /* lock for protecting */

void swapbytes(void *input, size_t len)
{
	unsigned int i;
	unsigned char *in = (unsigned char *)input, tmp;

	for (i = 0; i < len / 2; i++) {
		tmp = *(in + i);
		*(in + i) = *(in + len - i - 1);
		*(in + len - i - 1) = tmp;
	}
}

static unsigned char cal_checksum(unsigned char *buf, unsigned long size)
{
	unsigned char sum = 0;

	while (size > 0) {
		sum += (*(buf++));
		size--;
	}
	return ((~sum)+1);
}

static unsigned char valid_checksum(unsigned char *buf, unsigned long size)
{
	unsigned char sum = 0;

	while (size > 0) {
		sum += (*(buf++));
		size--;
	}
	return sum;
}

static int psu_read_fru(struct gxp_psu_drvdata *drvdata,
			u8 offset, u8 length, u8 *value)
{
	struct i2c_client *client = drvdata->client;
	unsigned char buf_tx[4] = {(client->addr << 1), READ_FRU_CMD, offset, length};
	unsigned char tx[4] = {0};
	unsigned char chksum = cal_checksum(buf_tx, 4);
	int ret = 0;
	struct i2c_msg msgs[2] = {0};

	value[0] = '\0';
	//	if (! drvdata->present )
	//		return -EOPNOTSUPP;

	tx[0] = READ_FRU_CMD;
	tx[1] = offset;
	tx[2] = length;
	tx[3] = chksum;
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].buf = tx;
	msgs[0].len = 4;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = value;
	msgs[1].len = length;
	mutex_lock(&drvdata->update_lock);
	ret = i2c_transfer(client->adapter, msgs, 2);
	mutex_unlock(&drvdata->update_lock);
	if (ret < 0) {
		dev_dbg(&client->dev,
			"gxppsu_i2c_tx_fail addr:0x%x offest:0x%x length:0x%x chk:0x%x ret:0x%x\n",
			client->addr, offset, length, chksum, ret);
	} else {
		ret = length;
	}

        return ret;
}

static ssize_t eeprom_file_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	int ret;
	struct device *dev = kobj_to_dev(kobj);
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);

	ret = psu_read_fru(drvdata, off, count, buf);
	return ret;
}

static const struct bin_attribute eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = 0444,
	},
	.size =  256,
	.read = eeprom_file_read,
};

static int psu_read_reg_word(struct gxp_psu_drvdata *drvdata,
			u8 reg, u16 *value)
{
	struct i2c_client *client = drvdata->client;
	unsigned char buf_tx[3] = {(client->addr << 1), READ_REG_CMD, reg};
	unsigned char buf_rx[3] = {0};
	unsigned char tx[3] = {0};
	unsigned char rx[3] = {0};
	unsigned char chksum = cal_checksum(buf_tx, 3);
	struct i2c_msg msgs[2] = {0};
	int ret = 0;

	tx[0] = 0;
	tx[1] = reg;
	tx[2] = chksum;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].buf = tx;
	msgs[0].len = 3;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = rx;
	msgs[1].len = 3;
	mutex_lock(&drvdata->update_lock);
	ret = i2c_transfer(client->adapter, msgs, 2);
	mutex_unlock(&drvdata->update_lock);
	if (ret < 0) {
		dev_dbg(&client->dev,
			"gxppsu_i2c_tx_fail addr:0x%x reg:0x%x chk:0x%x ret:0x%x\n",
			client->addr, reg, chksum, ret);
		return ret;
	}

	buf_rx[0] = rx[0];
	buf_rx[1] = rx[1];
	buf_rx[2] = rx[2];
	if (valid_checksum(buf_rx, 3) != 0) {
		dev_dbg(&client->dev,
			"gxppsu_checksum_fail addr:0x%x reg:0x%x, data:%x %x %x\n",
			client->addr, reg, rx[0], rx[1], rx[2]);
		return -EAGAIN;
	}

	*value = rx[0] + (rx[1] << 8);
	dev_dbg(&client->dev, "chk:%x val:%x, %x %d\n", chksum, *value,
			client->addr << 1, reg);
	return ret;
}

static int gxppsu_update_client(struct device *dev, u8 reg)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;

//	if ( !drvdata->present )
//		return -EOPNOTSUPP;
	switch (reg) {
	case REG_IN_PWR:
		ret = psu_read_reg_word(drvdata, REG_IN_PWR,
					&drvdata->input_power);
		break;
	case REG_IN_VOL:
		ret = psu_read_reg_word(drvdata, REG_IN_VOL,
					&drvdata->input_voltage);
		break;
	case REG_IN_CUR:
		ret = psu_read_reg_word(drvdata, REG_IN_CUR,
					&drvdata->input_current);
		break;
	case REG_OUT_PWR:
		ret = psu_read_reg_word(drvdata, REG_OUT_PWR,
					&drvdata->output_power);
		break;
	case REG_OUT_VOL:
		ret = psu_read_reg_word(drvdata, REG_OUT_VOL,
					&drvdata->output_voltage);
		break;
	case REG_OUT_CUR:
		ret = psu_read_reg_word(drvdata, REG_OUT_CUR,
					&drvdata->output_current);
		break;
	case REG_INLET_TEMP:
		ret = psu_read_reg_word(drvdata, REG_INLET_TEMP,
					&drvdata->inlet_temp);
		break;
	case REG_FAN_SPEED:
		ret = psu_read_reg_word(drvdata, REG_FAN_SPEED,
					&drvdata->fan_speed);
		break;
	default:
		dev_err(&drvdata->client->dev, "gxppsu_error_reg 0x%x\n", reg);
		return -EOPNOTSUPP;
	}

	return ret;
}

static ssize_t show_power_input(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_IN_PWR;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%u\n", drvdata->input_power);
}

static ssize_t show_in_input(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_IN_VOL;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;
	// reporting is done through 1/32 or VRMS
	return sprintf(buf, "%u\n", drvdata->input_voltage);
}

static ssize_t show_curr_input(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_IN_CUR;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;
	// reporting is done through 1/64 of amps
	return sprintf(buf, "%u\n", drvdata->input_current);
}

static ssize_t show_power_output(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_OUT_PWR;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", drvdata->output_power);
}

static ssize_t show_in_output(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_OUT_VOL;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;

	// out vol is 1/256 of V
	// but their is a ration of 1/32 on voltage
	// so divide only by 8
	drvdata->output_voltage = drvdata->output_voltage / 8;
	return sprintf(buf, "%u\n", drvdata->output_voltage);
}

static ssize_t show_curr_output(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_OUT_CUR;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;
	// out current is 1/64 Amps
	// drvdata->output_current = drvdata->output_current / 64;
	return sprintf(buf, "%u\n", drvdata->output_current);
}

static ssize_t show_temp_input(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	s32 value;
	u8 reg = REG_INLET_TEMP;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0)
		return ret;

	// 1/64 of Celsius
	value = (s32)(drvdata->inlet_temp);
	// value = ( value / 64) * 1000;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_fan_input(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct gxp_psu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	u8 reg = REG_FAN_SPEED;

	ret = gxppsu_update_client(dev, reg);
	if (ret < 0) {
		// printk("gxp: error while updating client @ %x\n", drvdata->client->addr);
		return ret;
	}
	// rpm's
	return sprintf(buf, "%u\n", drvdata->fan_speed);
}

static ssize_t show_curr2_label(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "iout1\n");
}

static ssize_t show_power2_label(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "pout1\n");
}

static ssize_t show_in2_label(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "vout1\n");
}

static ssize_t show_in1_label(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "vin1\n");
}

// that stuff is polled every second
static ssize_t report_failure(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	// return sprintf(buf, "1\n");
	return sprintf(buf, "0\n");
}

static SENSOR_DEVICE_ATTR(power1_input, 0444, show_power_input, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, 0444, show_in_input, NULL, 1);
static SENSOR_DEVICE_ATTR(curr1_input, 0444, show_curr_input, NULL, 2);
static SENSOR_DEVICE_ATTR(power2_input, 0444, show_power_output, NULL, 3);
static SENSOR_DEVICE_ATTR(in2_input, 0444, show_in_output, NULL, 4);
static SENSOR_DEVICE_ATTR(curr2_input, 0444, show_curr_output, NULL, 5);
static SENSOR_DEVICE_ATTR(temp1_input, 0444, show_temp_input, NULL, 6);
static SENSOR_DEVICE_ATTR(fan1_input, 0444, show_fan_input, NULL, 7);
static SENSOR_DEVICE_ATTR(curr2_label, 0444, show_curr2_label, NULL, 8);
static SENSOR_DEVICE_ATTR(power2_label, 0444, show_power2_label, NULL, 9);
static SENSOR_DEVICE_ATTR(in2_label, 0444, show_in2_label, NULL, 10);
static SENSOR_DEVICE_ATTR(in2_alarm, 0444, report_failure, NULL, 15);
// Add label
//
static SENSOR_DEVICE_ATTR(in1_label, 0444, show_in1_label, NULL, 20);

static struct attribute *gxp_psu_attrs[] = {
	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power2_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_curr2_label.dev_attr.attr,
	&sensor_dev_attr_power2_label.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(gxp_psu);

static const struct of_device_id gxp_psu_of_match[] = {
	{ .compatible = "hpe,gxp-psu" },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_psu_of_match);

static int gxp_psu_probe(struct i2c_client *client)
{
	struct gxp_psu_drvdata *drvdata;
	struct device *hwmon_dev;
	struct device_node *dn = client->dev.of_node;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_EMUL)) {
		return -EIO;
	}

	dev_info(&client->dev, "gxp-psu: node name %s\n", dn->full_name);

	drvdata = devm_kzalloc(&client->dev, sizeof(struct gxp_psu_drvdata),
			GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	dev_info(&client->dev, "gxp-psu: set client\n");
	drvdata->client = client;
	i2c_set_clientdata(client, drvdata);

	dev_info(&client->dev, "gxp-psu: init mutex\n");
	mutex_init(&drvdata->update_lock);
	mutex_init(&psu_drv_lock);

	drvdata->hwmon_dev = NULL;
	// We need to check if the PSU is really present before registering

	mutex_lock(&psu_drv_lock);

	dev_info(&client->dev, "gxp-psu: create hwmon\n");
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev, "PowerSupply",
							   drvdata,
							   gxp_psu_groups);
	dev_info(&client->dev, "gxp-psu: registered\n");
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);
	dev_info(&client->dev, "gxp-psu: display debug info\n");
	dev_info(&client->dev, "%s:'%s' addr:0x%x\n", dev_name(hwmon_dev),
			client->name, client->addr);

	//	hwmon_dev = NULL;
	// Let's create the eeprom entry
	ret = sysfs_create_bin_file(&client->dev.kobj, &eeprom_attr);
	if (ret) {
		dev_err(&client->dev, "failed to create sysbin");
		return -EIO;
	}
	dev_info(&client->dev, "gxp-psu: assigning hwmon_dev\n");
	drvdata->hwmon_dev = hwmon_dev;
	// psucount++;
	dev_info(&client->dev, "gxp-psu: unlock\n");
	mutex_unlock(&psu_drv_lock);

	return 0;
}

void gxp_psu_remove(struct i2c_client *client)
{
	hwmon_device_unregister(&client->dev);
	sysfs_remove_bin_file(&client->dev.kobj, &eeprom_attr);
}

static struct i2c_driver gxp_psu_driver = {
	.class = I2C_CLASS_HWMON,
	.probe		= gxp_psu_probe,
	.remove		= gxp_psu_remove,
	.driver = {
		.name	= "gxp-psu",
		.of_match_table = gxp_psu_of_match,
	},
};
module_i2c_driver(gxp_psu_driver);

MODULE_AUTHOR("Louis Hsu <kai-hsiang.hsu@hpe.com>");
MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("HPE GXP PSU driver");
MODULE_LICENSE("GPL");

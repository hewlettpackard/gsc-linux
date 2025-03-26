// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>

#define V_EEPROM_EEPROM_MAX_SIZE 256

struct v_eeprom_data {
	struct mutex lock; /* Protect Reads */
	struct nvmem_device *nvmem;
	struct regmap *regmap;
	struct i2c_client *client;
	u32 byte_len;
	u16 page_size;
	u8 flags;
};

static int v_eeprom_read(void *priv, unsigned int offset, void *buf,
			 size_t size)
{
	struct v_eeprom_data *v_eeprom = priv;
	void *p;

	mutex_lock(&v_eeprom->lock);

	p = memremap(0xA0017F00, 256, MEMREMAP_WB);
	if (!p) {
		pr_err("gxp-v-eeprom cannot map memory block\n");
		mutex_unlock(&v_eeprom->lock);
		return -ENOMEM;
	}

	if (offset + size > V_EEPROM_EEPROM_MAX_SIZE) {
		pr_err("gxp-v-eeprom read exceeded capacity\n");
		memunmap(p);
		mutex_unlock(&v_eeprom->lock);
		return -EINVAL;
	}
	memcpy(buf, p + offset, size);
	memunmap(p);
	mutex_unlock(&v_eeprom->lock);

	return size;
}

static ssize_t v_eeprom_file_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr, char *buf,
				  loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct i2c_client *client = to_i2c_client(dev);
	struct v_eeprom_data *v = i2c_get_clientdata(client);

	return v_eeprom_read(v, off, buf, count);
}

static const struct of_device_id v_eeprom_match[] = {
	{ .compatible = "hpe,gxp-v-eeprom" },
	{ /* null */ },
};

static const struct bin_attribute eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = 0444,
	},
	.size =  V_EEPROM_EEPROM_MAX_SIZE,
	.read = v_eeprom_file_read,
};

/*
 * @brief This function is called probing the driver
 */

static int v_eeprom_probe(struct i2c_client *client)
{
	struct regmap_config regmap_config = {};
	struct nvmem_config nvmem_config = {};
	struct device *dev = &client->dev;
	struct v_eeprom_data *v_eeprom;
	struct regmap *regmap;
	int err;
	u8 buffer[256];
	int k;
	u32 page_size;

	dev_info(dev, "gxp-v-eeprom probe - Probing function!\n");
	dev_info(dev, "gxp-v-eeprom: probe\n");

	v_eeprom = devm_kzalloc(dev, sizeof(struct v_eeprom_data), GFP_KERNEL);
	if (!v_eeprom)
		return -ENOMEM;

	/* Check for device properties */

	if (device_property_read_u32(dev, "pagesize", &page_size)) {
		dev_info(dev, "gxp-v-eeprom probe - Error! Device property 'page_size' not found!\n");
		return -1;
	}

	regmap_config.val_bits = 8;
	regmap_config.reg_bits = 8;

	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	mutex_init(&v_eeprom->lock);

	v_eeprom->page_size = page_size;
	v_eeprom->byte_len = 256;
	v_eeprom->flags = 0;
	v_eeprom->regmap = regmap;
	v_eeprom->client = client;

	if (device_property_present(dev, "label")) {
		nvmem_config.id = NVMEM_DEVID_NONE;
		err = device_property_read_string(dev, "label",
						  &nvmem_config.name);
		if (err)
			return err;
	} else {
		nvmem_config.id = NVMEM_DEVID_AUTO;
		nvmem_config.name = dev_name(dev);
	}

	nvmem_config.read_only = true;
	nvmem_config.dev = dev;
	nvmem_config.owner = THIS_MODULE;
	nvmem_config.reg_read = v_eeprom_read;
	nvmem_config.priv = v_eeprom;
	nvmem_config.stride = 1;
	nvmem_config.word_size = 1;
	nvmem_config.size = V_EEPROM_EEPROM_MAX_SIZE;

	i2c_set_clientdata(client, v_eeprom);
	v_eeprom->nvmem = devm_nvmem_register(dev, &nvmem_config);
	if (IS_ERR(v_eeprom->nvmem)) {
		dev_info(dev, "gxp-v-eeprom probe - Error Register\n");
		return PTR_ERR(v_eeprom->nvmem);
	}
	err = sysfs_create_bin_file(&client->dev.kobj, &eeprom_attr);
	if (err)
		return err;
	dev_info(dev, "gxp-v-eeprom probe - Register OK\n");

	return 0;
}

/*
 * @brief This function is called unloading the driver
 */
void v_eeprom_remove(struct i2c_client *client)
{
	sysfs_remove_bin_file(&client->dev.kobj, &eeprom_attr);
}
MODULE_DEVICE_TABLE(of, v_eeprom_match);

static struct i2c_driver v_eeprom_driver = {
	.probe_new = v_eeprom_probe,
	.remove = v_eeprom_remove,
	.driver = {
		.name = "hpe,gxp-v-eeprom",
		.of_match_table = v_eeprom_match,
	},

};

/*
 * @brief This function is called when the module is loaded into the kernel
 */
static int __init v_eeprom_init(void)
{
	pr_info("Init the gxp-v-eeprom driver...\n");
	pr_info("gxp-v-eeprom: init\n");
	if (i2c_add_driver(&v_eeprom_driver)) {
		pr_err("gxp-v-eeprom  driver init - Error! Could not load driver\n");
		return -1;
	}
	pr_info("gxp-v-eeprom driver init -OK\n");
	return 0;
}

/*
 * @brief This function is called when the module is removed from the kernel
 */
static void __exit v_eeprom_exit(void)
{
	i2c_del_driver(&v_eeprom_driver);
}

module_init(v_eeprom_init);
module_exit(v_eeprom_exit);

MODULE_AUTHOR("Grant OConnor");
MODULE_DESCRIPTION("Kernel modules to get EEPROM data");
MODULE_LICENSE("GPL");

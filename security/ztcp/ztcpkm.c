// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/init.h>
#ifdef MODULE
#include <linux/module.h>
#endif
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/security.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include <linux/key.h>
#include <linux/namei.h>
#include <linux/version.h>

#include "ztcpkm_core.h"
#include "ztcpkm_netlink.h"
#include "ztcpkm_config.h"
#include "ztcputil.h"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

static int ztcpkm_major = -1;
static struct class *ztcpkm_class = NULL;
static struct cdev ztcpkm_cdev;

#define ZCLIENTIOC 1075331840

struct ztcpkm_msg
{
	uint16_t msg_type;
	void *buf;
};

static long ztcpkm_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long retval = 0;

	switch (cmd) {
	default:
		ztcpkm_log_kernel("ztcpkm_dev_ioctl - unknown command %d\n", cmd);
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static const struct file_operations ztcpkm_dev_ioctl_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ztcpkm_dev_ioctl,
};

static void device_cleanup(int device_created)
{
	if (device_created) {
		device_destroy(ztcpkm_class, ztcpkm_major);
		cdev_del(&ztcpkm_cdev);
	}
	if (ztcpkm_class)
		class_destroy(ztcpkm_class);
	if (ztcpkm_major != -1)
		unregister_chrdev_region(ztcpkm_major, 1);
}

static int ztcpkm_init(void)
{
	int ret = 0;
	int device_created = 0;

	// https://stackoverflow.com/questions/48869201/could-i-find-all-kernel-modules-that-are-running-even-they-are-hide
	//list_del_init(&__this_module.list);
	//kobject_del(&THIS_MODULE->mkobj.kobj);

	if (alloc_chrdev_region(&ztcpkm_major, 0, 1, "ztcpkm_device") < 0)
		goto device_error;
	if ((ztcpkm_class = class_create("ztcpkm_class")) == NULL)
		goto device_error;
	cdev_init(&ztcpkm_cdev, &ztcpkm_dev_ioctl_ops);
	if (cdev_add(&ztcpkm_cdev, ztcpkm_major, 1) == -1)
		goto device_error;
	if (device_create(ztcpkm_class, NULL, ztcpkm_major, NULL, "ztcpkm") == NULL)
		goto device_error;
	device_created = 1;
	pr_debug("ztcpkm_device driver create\n");

	ret = ztcpkm_config_init();
	if (ret != 0) {
		ztcpkm_log_kernel("Failed to init config\n");
		goto config_error;
	}

	if (!ztcpkm_netlink_init()) {
		ztcpkm_log_kernel("Failed to init netlink\n");
		ret = -1;
		goto netlink_error;
	}

	ztcpkm_log_info("ztcpkm module loaded (%s)\n", ___ZTCPKM_COMMIT);
	return ret;

netlink_error:
	if (!ztcpkm_netlink_fini()) {
		ztcpkm_log_kernel("Failed to teardown netlink\n");
	}
config_error:
        ztcpkm_config_fini();
device_error:
	device_cleanup(device_created);
	ztcpkm_log_err("ztcpkm initialization failure (%d)\n", ret);
	return ret;
}

static void ztcpkm_exit(void)
{
	device_cleanup(1);

	if (!ztcpkm_netlink_fini()) {
		ztcpkm_log_kernel("Failed to teardown netlink\n");
	}

        ztcpkm_config_fini();
	ztcpkm_log_info("ztcpkm module unloaded (%s)\n", ___ZTCPKM_COMMIT);
}

module_init(ztcpkm_init);
module_exit(ztcpkm_exit);
MODULE_VERSION(___ZTCPKM_COMMIT);
MODULE_LICENSE("GPL");

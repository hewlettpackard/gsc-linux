// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2025 Hewlett Packard Enterprise Development LP */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/of.h>

#define IOP_SUICIDE_RESET    0x01
#define EXP_CHASSIS_AC_CYCLE_IN_PROGRESS 0xBC
#define CHASSIS_AC_NOTIF_INT_STATE 0xBE
#define EXP_CHASSIS_AC_RDY 0x58
#define MULTI_NODE_CHASSIS_AC_TIMEOUT (30*1000)


static int gsc_restart_handler(struct notifier_block *this, unsigned long mode,
			    void *cmd)
{
	void __iomem *iopbase = NULL;
	void __iomem *expbase = NULL;
	uint8_t progress_val = 0;
	uint8_t rdy_val = 0;

	iopbase = ioremap((phys_addr_t)0xC0000000, 0x100);
	if (!iopbase) {
		pr_err("error during ioremap of iopbase\n");
		return NOTIFY_DONE;
	}


	expbase = ioremap((phys_addr_t)0xD1000000, 0x100); //Memmap exp bus
	if (!expbase) {
		iounmap(iopbase);
		pr_err("error during ioremap of expbase\n");
		return NOTIFY_DONE;
	}

	progress_val = readb(expbase + CHASSIS_AC_NOTIF_INT_STATE);
	if (progress_val & 0x08) {
		progress_val = readb(expbase + EXP_CHASSIS_AC_CYCLE_IN_PROGRESS);
		rdy_val = readb(expbase + EXP_CHASSIS_AC_RDY) | 0x40;
		progress_val &= ~0x08;

		writeb(rdy_val, expbase + EXP_CHASSIS_AC_RDY);
		writeb(progress_val, expbase + EXP_CHASSIS_AC_CYCLE_IN_PROGRESS);
		pr_info("Waiting for chassis AC cycle\n");
		mdelay(MULTI_NODE_CHASSIS_AC_TIMEOUT);
	}
	// Initiate IOP suicide Reset
	pr_info("Initiating ASIC Reset\n");
	writeb(IOP_SUICIDE_RESET, iopbase + 0x00); // Initiate IOP suicide Reset
	iounmap(expbase);
	iounmap(iopbase);
	mdelay(2000);

	// below message indicates that ASIC reset is not successful.
	pr_emerg("%s: unable to reset ASIC\n", __func__);
	return NOTIFY_DONE;
}

static struct notifier_block gsc_reboot_nb = {
	.notifier_call = gsc_restart_handler,
	.priority = 255,
};


static int gsc_poweroff_probe(struct platform_device *pdev)
{
	int err;

	//according to kernel documentation,Restart handlers are expected
	//to be registered from non-architecture code, typically from drivers.
	//Multiple restart handlers may exist; for example, one restart handler
	//might restart the entire system, while another only restarts the CPU.
	//In such cases, the restart handler which only restarts part of the
	//hardware is expected to register with low priority to ensure that
	//it only runs if no other means to restart the system is available.
	//hence register the handler that restart the system with Highest priority 255
	err = register_restart_handler(&gsc_reboot_nb);
	if (err != 0) {
		pr_err("cannot register restart handler for GSC power off(err=%d)\n", err);
		return err;
	}
	pr_info("restart handler for GSC power off successfully registered\n");
	return 0;
}

static void gsc_poweroff_remove(struct platform_device *pdev)
{
	unregister_restart_handler(&gsc_reboot_nb);
	return;
}

static const struct of_device_id gsc_poweroff_of_match[] = {
	{ .compatible = "hpe,gsc-poweroff" },
	{},
};

MODULE_DEVICE_TABLE(of, gsc_poweroff_of_match);

static struct platform_driver gsc_poweroff_driver = {
	.probe = gsc_poweroff_probe,
	.remove = gsc_poweroff_remove,
	.driver = {
		.name = "gsc-poweroff",
		.of_match_table = of_match_ptr(gsc_poweroff_of_match),
	},
};
module_platform_driver(gsc_poweroff_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeke Kumar Gochhayat <jeke.kum.gochhayat@hpe.com>");
MODULE_DESCRIPTION("GSC power off driver");

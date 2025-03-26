// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This module contains logic to control the DP TX Core.
 *
 */


#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/types.h>
#include <asm/unistd.h>
#include <linux/io.h>
#include <linux/semaphore.h>

#include "vasregs.h"
#include "dprx.h"


/*********************************************************************************
 *  Description:
 *     Initialize the DisplayPort RX.
 *     For the current implementaion, the RX port is not used.
 *     The Phy reset must be initialized prior
 *     to reading registers for the phy.
 *
 *  Return:
 ********************************************************************************/
void dprx_init(void)
{
	uint32_t rev;

	/// \brief Validate DisplayPort RX Revision (DPRX Core)
	///        Supported Core ID:  Bit 31:16 - 0x000B
	///        Supported Core Rev: Bit 15:0  - 0x0309
	///        and
	///        Supported Core Rev: Bit 15:0  - 0x0401
	rev = asicregister_read32(0x0FC | (unsigned long)R_DPRX_BASE);
	pr_debug("%s: DPRX Core: 0x%x\n", __func__, rev);

	/// \note The DPRX Phy Reset must be set to a value prior to reading the Phy.
	///       If not set, the address will incur an access fault.
	///       Confirmed with ASIC team, this is correct behavior.
	///       Write a 0 for normal operation in the event the PHY config has been
	///       programmed.
	//asicregister_write32(0x200 | R_DPRX_BASE, 0x00);//register is deprecated
}

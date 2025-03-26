// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This module contains the main logic for DisplayPort Link Policy.
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
#include "txlink.h"
#include "dpcd.h"
#include "dprx.h"
#include "dptx.h"
#include "dptxaux.h"
#include "edid.h"

//#include "autotest.h"
//#include "dpwa1.h"

typedef struct {
	uint32_t tu_cfg_src;
	uint8_t  link_rate;
	uint8_t  lane_cnt;   /// Enhanced framing noted in top bit
	uint8_t  rsvd[2];
	union {
		struct {
			uint32_t phy_update     : 1;
			uint32_t edid_generic   : 1;  /// Generic EDID is only used in the event of an error
		} flag;
		uint32_t val;
	} flags;
} link_cfg_t;

/// \todo Determine if this should be passed in from a higher level?
/// \brief DPCD Fields for local use
static dpcd_rx_capabilty_t g_dpcd;
static edid_t              g_edid;
static link_cfg_t          g_link;

static uint8_t g_edid_generic[] = {

	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x22, 0xF0, 0x49, 0x29, 0x01, 0x01, 0x01, 0x01,
	0x0B, 0x15, 0x01, 0x01, 0x04, 0xA5, 0x33, 0x1D, 0x78, 0x26, 0x1E, 0x55, 0xA0, 0x59, 0x56, 0x9F,
	0x27, 0x0D, 0x50, 0x54, 0xA1, 0x08, 0x00, 0x81, 0xC0, 0x81, 0x80, 0x95, 0x00, 0xB3, 0x00, 0xD1,
	0xC0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0xFD, 0x1E, 0x11, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x32, 0x4C, 0x18,
	0x5E, 0x11, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x48,
	0x50, 0x20, 0x4C, 0x41, 0x32, 0x33, 0x30, 0x36, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x33, 0x43, 0x51, 0x31, 0x31, 0x31, 0x30, 0x48, 0x57, 0x52, 0x0A, 0x20, 0x20, 0x00, 0xC0
};

/************************************************************************************************
 *  Description:
 *     Configure the DP link.
 *
 *
 *  Note:
 *     Based on the VESA specification for: DisplayPort Ver 1.1a
 *
 ************************************************************************************************/
static uint32_t config_link(link_cfg_t *pCfg, const uint16_t cReg)
{
	uint32_t retv = 0;
	int    i;

	/// Disable auto programmer
	asicregister_write16(GD_MCFG_GDCFG, (cReg & ~(0x20)));  // IOPBASE + 01c0h

	if (pCfg->flags.flag.phy_update) {
		dptx_cfg_phy(pCfg->link_rate);
		pCfg->flags.flag.phy_update = 0;
	}

	/// Only support 2 bit rates.
	/// \todo Need to consider a reduction in lanes; must consider impact on
	///       resolution and ability to display video.
	for (i = 0; i < 2; i++) {
		/// \todo Add for loop for bit rate change and or number of lane changes
		retv = dptx_train(pCfg->link_rate, pCfg->lane_cnt);
		if (!retv) {
			/// \brief [R084] - Will be enabled by the auto programmer.
			asicregister_write16(GD_MCFG_GDCFG, (cReg|0x20));// IOPBASE + 01c0h
			break;
		}
		if (pCfg->link_rate == DPCD_LINK_RATE_2_7) {
			pr_err("WARNING: %s: Link training failed; Attempt LBR (%u).\n", __func__, retv);
			pCfg->link_rate = DPCD_LINK_RATE_1_62;
			dptx_cfg_phy(pCfg->link_rate);
		} else {
			pr_err("FATAL: %s: Link training failed (%u).\n", __func__, retv);
			break;
		}
	}
	return retv;
}

/************************************************************************************************
 *  Description:
 *
 *
 *  Args:
 *     uint8_t - value to set
 *
 *  Return:
 *     None
 *
 *  Note:
 *     Based on the VESA specification for: DisplayPort Ver 1.1a
 *
 ************************************************************************************************/
static void dpcd_power(uint8_t cVal)
{
	int    i;
	uint32_t retv;

	/// Attempt to wake monitor up a max of 2 times.
	for (i = 0; i < 2; i++) {
		retv = dptxaux_write(DPCD_SET_POWER, 0x01, &cVal);
		if (retv == 0)
			break;

		pr_err("FATAL: %s: Unable to power display: %u\n", __func__, retv);
		/// Wait 1 ms and try again; give time for the display to wake up.
		//usleep(1*1000);
		udelay(1*1000);

	}
}

/************************************************************************************************
 *  Description:
 *     Read the DisplayPort Device Service IRQ vector.  A description of the
 *     different services can be found in the DPCD register definition, 201h.
 *
 *  Args:
 *     uint8_t      - DPCD revision
 *     uint8_t      - EDID checksum
 *
 *  Note:
 *
 ************************************************************************************************/
static void dpcd_irq(const uint8_t cRev, const uint8_t cChksum)
{
	uint32_t retv;
	union reg_201h {
		struct {
			uint8_t remote_control : 1;
			uint8_t automated_test : 1;
			uint8_t cp_irq         : 1;
			uint8_t rsvd_3         : 3;
			uint8_t sink_irq       : 1;
			uint8_t rsvd_7         : 1;
		};
		uint8_t cmd;
	};

	union reg_201h val;

	retv = dptxaux_read(DPCD_DEVICE_SERV_IRQ_VECTOR, sizeof(val), &val.cmd);
	if (retv == 0) {
		pr_debug("INFO: %s: Service IRQ Vector 0x%x\n", __func__, val.cmd);
		switch (val.cmd) {
		case DPCD_IRQ_AUTOMATED_REQ:
			//autotest_req(cRev,cChksum);//commented
		break;
		default:
			pr_err("WARNING: %s: Unsupported IRQ vector: 0x%x.\n", __func__, val.cmd);
		break;
		}
	} else {
		pr_err("FATAL: %s: Failed to read service vector. (%u)\n", __func__, retv);
	}
}

/************************************************************************************************
 *  Description:
 *     Determine the capabilities of the connected device.
 *     The cabilities are contained in the first 12 bytes (0 based).
 *
 *  Args:
 *     pCap -  pointer to DPCD data typedef
 *     pCfg -  track configuration of link
 *
 *  Note:
 *     Based on the VESA specification for: DisplayPort Ver 1.1a
 *
 ************************************************************************************************/
static uint32_t dpcd_cfg(dpcd_rx_capabilty_t *pCap, link_cfg_t *pCfg)
{
	uint32_t retv = 0;
	uint8_t  curr_link_rate = pCfg->link_rate;

	/// Read the DPCD from the connected device
	retv = dptxaux_read(DPCD_REV, 0x0c, (uint8_t *)pCap);
	/// Now compare the capabilities and setup link configuration
	if (retv == 0) {
		if (pCap->max_link_rate <= 20) {
			/// Set link rate
			if (pCap->max_link_rate >= DPCD_LINK_RATE_2_7)
				pCfg->link_rate = DPCD_LINK_RATE_2_7;
			else
				pCfg->link_rate = DPCD_LINK_RATE_1_62;

		} else {
			pr_err("%s: Unknown link rate from DPCD 0x%02x\n", __func__, pCap->max_link_rate);
			pr_err("Wait %d ms\n", 100);
			//usleep(100*1000);
			usleep_range(100*1000, 200*1000);
		}

		/// Set lanes
		pCfg->lane_cnt = pCap->max_lane_cnt & 0x80;
		if ((pCap->max_lane_cnt & 0x01f) >= 0x2)
			pCfg->lane_cnt = 0x02;
		else
			pCfg->lane_cnt = 0x01;

		/// Set enhanced framing bit
		pCfg->lane_cnt |= (pCap->max_lane_cnt & 0x80);
		/// Determine if PHY needs to be reprogrammed
		if (curr_link_rate != pCfg->link_rate)
			pCfg->flags.flag.phy_update = 1;

	}

	return (retv);
}

/************************************************************************************************
 *  Description:
 *     Determine the link/Status of the connected device.
 *     The link status bits are contained in the first 16 bytes (0 based).
 *     This function will return the lane status for a specified lane.
 *
 *  Args:
 *     cLanes - Determine which lanes to read.
 *              0 - for lane status of 0 & 1
 *              1 - for lane status of 2 & 3 (Only 2 lanes supported at this time)
 *  Return:
 *     val - value of the requested register
 *  Note:
 *     Based on the VESA specification for: DisplayPort Ver 1.1a
 *     The VESA spec states registers 200h - 205h should be read.
 *
 *     The current HW only supports 2 lanes.
 ************************************************************************************************/
static uint8_t lanestatus(const uint8_t cLanes)
{
	uint32_t retv;
	uint8_t  val[6];

	/// \brief Read the DPCD from the connected device
	/// \todo Determine what to do with the other registers which are read.
	retv = dptxaux_read(DPCD_SINK_CNT, sizeof(val), val);
	if (retv == 0) {
		pr_debug("INFO: %s: Sink Status 0x%x\n", __func__, val[2+cLanes]);
	} else {
		pr_debug("FATAL: %s: Failed to read link status. (%u)\n", __func__, retv);
		val[2+cLanes] = 0;
	}
	return (val[2+cLanes]);
}

/************************************************************************************************
 *  Description:
 *     Sink device has sent notification something has changed;
 *     determine needs and service sink device.
 *
 *  Note:
 *     Link Policy based on the DisplayPort 1.1a
 *
 ************************************************************************************************/
void txlink_service(const uint16_t cReg)
{
	uint8_t status;
	uint32_t reg;

	/// \note "Modification" to check for DisplayPort presence prior to servicing link.
	//if ((Success == event_check(EVT_DISPLAYPORT_PRESENT))) commented to avoid errors
	{
		/// \brief Determine status of current lanes
		/// \todo include logic to determine current config without having to read EDID & Capability field
		status = lanestatus(0);
		if ((status != 0x77) && (status != 0x07)) {
			/// Configure link
			if (config_link(&g_link, cReg) == 0) {
				/// Generate a hotplug event to the host to force vEDID
				/// interogation
				reg = asicregister_read32(DMCTL_DECR);
				asicregister_write32(DMCTL_DECR, reg | 0xc0000);
			}
		} else {
			/// Link is trained; enable auto programmer
			/// \todo Determine if any other actions are needed.
			asicregister_write16(GD_MCFG_GDCFG, (cReg|0x20));
		}
	}
}

/************************************************************************************************
 *  Description:
 *     End link policy for DP.
 *     Disable auto programmer first to avoid possible race condition; then disable main stream.
 *
 *  Note:
 *
 *
 ************************************************************************************************/
void txlink_end(const uint16_t cReg)
{
	uint32_t reg;

	//event_lower(EVT_DISPLAYPORT_PRESENT); commented to avoid error

	/// Disable auto programmer
	asicregister_write16(GD_MCFG_GDCFG, (cReg & ~(0x20)));  // IOPBASE + 01c0h

	/// End the main stream
	dptx_end();

	/// Disable virtual EDID
	asicregister_write8(VEDIDCTL_VEDIDEN, 0x00);

	/// Generate a hotplug event to the host to force EDID
	/// interogation
	reg = asicregister_read32(DMCTL_DECR);
	asicregister_write32(DMCTL_DECR, reg & 0xFFF3FFFF);

	pr_debug("VERBOSE: End Link Policy\n");
}

/************************************************************************************************
 *  Description:
 *     Start up link policy for DP.  New connection detected.
 *
 *  Note:
 *     Link Policy based on the DisplayPort 1.1a
 *
 ************************************************************************************************/
void txlink_start(const uint16_t cReg)
{
	uint32_t retv = 0;
	uint32_t reg;
	edid_t *pEdid = &g_edid;

	/// Attempt to power on the monitor.
	dpcd_power(1);

	/// Clear flags for new connection
	g_link.flags.val     = 0;

	/// Now compare the capabilities and setup link configuration
	/// Read the link capability field of the DPCD; configure link parameters
	retv = dpcd_cfg(&g_dpcd, &g_link);

	if (retv == 0) {

		/// Read EDID
		retv = edid_read_dp((uint8_t *)pEdid, sizeof(edid_t));
		if (retv != DPTX_AUX_NATIVE_ACK) {
			pr_err("WARNING: %s: Failed to read EDID.\n", __func__);
			/// Set flag to note generic EDID is being used.
			g_link.flags.flag.edid_generic = 1;
			pEdid = (edid_t *)g_edid_generic;
		}

		/// Write/enable virtual EDID
		edid_write_virt((uint8_t *)pEdid, sizeof(edid_t));
		asicregister_write8(VEDIDCTL_VEDIDEN, 0x01);

		/// \brief Check the service IRQ vector to determine if a special
		///        operation should be performed.
		/// \note Avoid duplicating code request if possible.
		/// \todo Determine how to integrate service request and core functionality.
		///       When service request happens, what should be the behavior?
		dpcd_irq(g_dpcd.dpcd_rev, pEdid->chksum);

		/// Configure link
		if (config_link(&g_link, cReg) == 0) {
			/// Generate a hotplug event to the host to force vEDID
			/// interogation
			reg = asicregister_read32(DMCTL_DECR);
			asicregister_write32(DMCTL_DECR, reg | 0xc0000);
			/// Update symbols per TU after successful link training.
			//dpwa1_sptu_update();//commented
		}
	} else {
		pr_err("FATAL: %s: Unable to read DPCD: %u\n", __func__, retv);
	}
	/// A HPD Event has been serviced; raise event to indicate connection
	/// present.
	//event_raise(EVT_DISPLAYPORT_PRESENT); commented to avoid errors
}

/*************************************************************************************************
 *  Description:
 *     Initialize link policy by setting up PHY to a 2.7Gbps; if not previously configured.
 *     If previously configured, determine if autoprogrammer should be turned back on.
 *
 *  Args:
 *     Semaphore - pointer to semaphore utilized for callback
 *     uint8_t     - current state of HPD; 1 = Connected; 0 = Disconnected
 *
 *  Note:
 *     Link Policy based on the DisplayPort 1.1a
 *
 ************************************************************************************************/
void txlink_init(struct semaphore *pSem, const uint8_t cState)
{
	uint32_t retv = 0;
	edid_t *pEdid = &g_edid;

	pr_err("%s: VERBOSE: Init Link Policy\n", __func__);

	/// \brief set up DisplayPort interfaces
	retv = dptx_init(pSem, &g_link.link_rate);
	dprx_init();

	if (retv) {
		/// The core was not fully configured; setup defaults for the PHY
		dptx_cfg_phy(DPCD_LINK_RATE_2_7);
		/// Update link information to determine current bandwidth setting
		/// of PHY
		g_link.link_rate = DPCD_LINK_RATE_2_7;
	}

	/// Is device in the connected state; update device information.
	if (cState) {
		/// Read DPCD and populate desired link configuration
		if (dpcd_cfg(&g_dpcd, &g_link))
			pr_err("WARNING: %s: Failed to read DPCD.\n", __func__);

		/// Read EDID and populate vEDID (virtual)
		if (edid_read_dp((uint8_t *)pEdid, sizeof(edid_t)) != DPTX_AUX_NATIVE_ACK) {
			pr_err("WARNING: %s: Failed to read local EDID.\n", __func__);
			/// Set flag to note a generic EDID is being used
			g_link.flags.flag.edid_generic = 1;
			/// Write a default EDID
			pEdid = (edid_t *)g_edid_generic;
		}
		/// Write virtual EDID
		edid_write_virt((uint8_t *)pEdid, sizeof(edid_t));
		/// Enable virtual EDID
		asicregister_write8(VEDIDCTL_VEDIDEN, 0x01);
		/// DisplayPort is present; raise event
		//event_raise(EVT_DISPLAYPORT_PRESENT); commented to avoid errors
	}
	///
	/// \todo Make sure the AUX Reply Data buffer is clean
	///       by reading???
}

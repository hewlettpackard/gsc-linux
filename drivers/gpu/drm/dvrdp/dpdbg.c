// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This module contains debug support for the DisplayPort.  Using this file for debug
 * will help abtract the chipset interface.
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
#include "dpcd.h"
#include "dpdbg.h"
#include "dptxaux.h"
#include "vf111tx.h"
#include "dptx.h"

static uint8_t *g_rx_cap_str[]  = {(uint8_t *)"DPCD_REV",
					(uint8_t *)"MAX_LNK",
					(uint8_t *)"MAX_LN",
					(uint8_t *)"MAX_DWNSPREAD",
					(uint8_t *)"",
					(uint8_t *)"",
					(uint8_t *)"MAIN_LNK_CH_CODING",
					(uint8_t *)"",
					(uint8_t *)"RX_PORT0_CAP0",
					(uint8_t *)"RX_PORT0_CAP1",
					(uint8_t *)"RX_PORT1_CAP0",
					(uint8_t *)"RX_PORT1_CAP1"};
static uint8_t *g_rx_cap_str1[]  = {(uint8_t *)"DWN_STRM_PORT0_CAP"};
static uint8_t *g_lnk_cfg_str[] = {(uint8_t *)"LINK_BW",
					(uint8_t *)"LANE_CNT",
					(uint8_t *)"TRAIN_PAT",
					(uint8_t *)"TRAIN_LANE0",
					(uint8_t *)"TRAIN_LANE1",
					(uint8_t *)"",
					(uint8_t *)"",
					(uint8_t *)"",
					(uint8_t *)"MAIN_LNK_CH_CODING"};
static uint8_t *g_lnk_cfg_str2[] = {(uint8_t *)"I2C_STATUS",
					(uint8_t *)"",
					(uint8_t *)"LINK_QUAL_LANE0",
					(uint8_t *)"LINK_QUAL_LANE1",
					(uint8_t *)"", (uint8_t *)"",
					(uint8_t *)"", (uint8_t *)"",
					(uint8_t *)"MSTM_CTRL"};
static uint8_t *g_lnk_stats_str[]  = {(uint8_t *)"SINK_CNT",
					(uint8_t *)"IRQ_VECTOR",
					(uint8_t *)"LANE0_1_STATS",
					(uint8_t *)"",
					(uint8_t *)"LANE_ALIGN_STATS",
					(uint8_t *)"SINK_STATS",
					(uint8_t *)"ADJ_REQ_LANE0_1",
					(uint8_t *)"",
					(uint8_t *)"TRAIN_SCORE_L0",
					(uint8_t *)"TRAIN_SCORE_L1"};
static uint8_t *g_lnk_stats_str2[] = {(uint8_t *)"SYM_ERR_CNT_L0",
					(uint8_t *)"SYM_ERR_CNT_L0",
					(uint8_t *)"SYM_ERR_CNT_L1",
					(uint8_t *)"SYM_ERR_CNT_L1"};
static uint8_t *g_device_spec[] = {(uint8_t *)"OUI 7:0",
					(uint8_t *)"OUI 15:8",
					(uint8_t *)"OUI 23:16"};

/************************************************************************************************
 *  Description:
 *     Print IEEE OUI of display.
 *
 *  Note:
 *     Not all Displays will have this support.
 *
 ************************************************************************************************/
static void print_oui(char *pStrg, uint8_t **pDesc, const uint32_t cAddr, const uint32_t cSz)
{
	int i;
	uint8_t buf[3];

	if (cSz != sizeof(buf)) {
		//fprintf(stderr,"FATAL: Buffer size do not match.\n");
		pr_err("FATAL: Buffer size do not match.\n");
		return;
	}

	memset(buf, 0, sizeof(buf));

	//fprintf(stderr,"  %s Device Specific Field\n",pStrg);
	pr_debug("  %s Device Specific Field\n", pStrg);
	if (dptxaux_read(cAddr, sizeof(buf), buf) == 0) {
		for (i = 0; i < sizeof(buf); i++, pDesc++) {
			//fprintf(stderr,"[%03lx] - 0x%02x   %s\n",i+cAddr,buf[i],*pDesc);
			pr_debug("[%03x] - 0x%02x   %s\n", i+cAddr, buf[i], *pDesc);
		}
	} else {
		//fprintf(stderr,"Failed to read %s Device Specific Field\n",pStrg);
		pr_err("Failed to read %s Device Specific Field\n", pStrg);
	}
}

/************************************************************************************************
 *  Description:
 *     Print all the valid registers of the DPCD.
 *
 *  Note:
 *     Using DisplayPort 1.1a
 *
 *  TODO:
 *     Split the function to differentiate between DPCD versions.
 ************************************************************************************************/
static void dpdbg_print_dpcd(void)
{
	uint8_t buf[16];
	uint8_t sz, sz_nxt;
	uint8_t ver;
	uint8_t oui_support;
	int i;

	memset(buf, 0, sizeof(buf));
	/// Determine number of registers to read.
	sz = sizeof(g_rx_cap_str)/sizeof(uint8_t *);
	if (dptxaux_read(DPCD_REV, sz, buf) == 0) {
		/// Save version for output
		ver = buf[0];
		//fprintf(stderr,"  DPCD Ver %d.%d\n",(ver>>4),(ver & 0x03));
		//fprintf(stderr,"  RX Capability Field\n");
		pr_debug("  DPCD Ver %d.%d\n", (ver>>4), (ver & 0x03));
		pr_debug("  RX Capability Field\n");

		for (i = 0; i < sz; i++) {
			//fprintf(stderr,"[%03x] - 0x%02x   %s\n",i,buf[i],g_rx_cap_str[i]);
			pr_debug("[%03x] - 0x%02x   %s\n", i, buf[i], g_rx_cap_str[i]);
		}

		/// Determine if display supports IEEE OUI.
		oui_support = (buf[0x07] & 0x80);

		/// This is for 1.1 only; 1.2 uses some of the reserved registers
		//fprintf(stderr,"[%03x] - [07f]  RESERVED\n",i);
		pr_debug("[%03x] - [07f]  RESERVED\n", i);
		sz = sizeof(g_rx_cap_str1)/sizeof(uint8_t *);
		memset(buf, 0, sizeof(buf));
		if (dptxaux_read(0x80, sizeof(buf), buf) == 0) {
		for (i = 0; i < sz; i++) {
			//fprintf(stderr,"[%03x] - 0x%02x   %s\n",i+0x80,buf[i],g_rx_cap_str1[i]);
			pr_debug("[%03x] - 0x%02x   %s\n", i+0x80, buf[i], g_rx_cap_str1[i]);
		}
	}

	memset(buf, 0, sizeof(buf));
	//fprintf(stderr,"  Link Configuration\n");
	pr_debug("  Link Configuration\n");
	sz = sizeof(g_lnk_cfg_str)/sizeof(uint8_t *);
	if (dptxaux_read(DPCD_LINK_BW_SET, sz, buf) == 0) {
		for (i = 0; i < sz; i++) {
			//fprintf(stderr,"[1%02x] - 0x%02x   %s\n",i,buf[i],g_lnk_cfg_str[i]);
			pr_debug("[1%02x] - 0x%02x   %s\n", i, buf[i], g_lnk_cfg_str[i]);
		}
	} else {
		//fprintf(stderr,"Failed to read %s\n","Link Configuration");
		pr_err("Failed to read %s\n", "Link Configuration");
	}
	/// Continue read
	if (ver == DPCD_VER_1_2) {
		memset(buf, 0, sizeof(buf));
		sz_nxt = sizeof(g_lnk_cfg_str2)/sizeof(uint8_t *);
		/// \note continue previous read
		if (dptxaux_read(DPCD_LINK_BW_SET + sz, sz_nxt, buf) == 0) {
			for (i = 0; i < sz_nxt; i++) {
				//fprintf(stderr,"[1%02x] - 0x%02x   %s\n",i+sz,buf[i],g_lnk_cfg_str2[i]);
				pr_debug("[1%02x] - 0x%02x   %s\n", i+sz, buf[i], g_lnk_cfg_str2[i]);
			}
		} else {
			//fprintf(stderr,"Failed to read %s\n","Link Configuration cont...");
			pr_err("Failed to read %s\n", "Link Configuration cont...");
		}
	}

	//fprintf(stderr,"  Link/Sink Status Field\n");
	pr_debug("  Link/Sink Status Field\n");
	/// Determine number of registers to read.
	sz = sizeof(g_lnk_stats_str)/sizeof(uint8_t *);
	if (dptxaux_read(DPCD_SINK_CNT, sz, buf) == 0) {
		for (i = 0; i < sz; i++) {
			//fprintf(stderr,"[2%02x] - 0x%02x   %s\n",i,buf[i],g_lnk_stats_str[i]);
			pr_debug("[2%02x] - 0x%02x   %s\n", i, buf[i], g_lnk_stats_str[i]);
		}
	} else {
		//fprintf(stderr,"Failed to read %s\n","Link/Sink Status Field");
		pr_err("Failed to read %s\n", "Link/Sink Status Field");
	}

	memset(buf, 0, sizeof(buf));
	/// Determine number of registers to read.
	sz = sizeof(g_lnk_stats_str2)/sizeof(uint8_t *);
	/// \note continue previous read
	if (dptxaux_read(DPCD_SYBL_ERR_CNT_LANE0, sz, buf) == 0) {
		for (i = 0; i < sz; i++) {
			//fprintf(stderr,"[2%x] - 0x%02x   %s\n",i+0x10,buf[i],g_lnk_stats_str2[i]);
			pr_debug("[2%x] - 0x%02x   %s\n", i+0x10, buf[i], g_lnk_stats_str2[i]);
		}
	} else {
		//fprintf(stderr,"Failed continued read %s\n","Link/Sink Status Field");
		pr_err("Failed continued read %s\n", "Link/Sink Status Field");
	}

	if (oui_support) {
		print_oui("Source", g_device_spec, DPCD_SRC_IEEE_OUI_0, sizeof(g_device_spec)/sizeof(uint8_t *));
		print_oui("Sink", g_device_spec, DPCD_SINK_IEEE_OUI_0, sizeof(g_device_spec)/sizeof(uint8_t *));
	}
	} else {
		//fprintf(stderr,"Failed to read %s\n","RX Capability Field");
		pr_err("Failed to read %s\n", "RX Capability Field");
	}
}

/************************************************************************************************
 *  Description:
 *     Debug command to force the PHY to transmit a specific link rate.
 *
 *  Args:
 *     uint32_t - Link Rate
 *     uint32_t - Number of lanes.
 *
 *  Note:
 *
 *
 ************************************************************************************************/
static void force_phy(const uint8_t cLink, const uint8_t cLanes)
{
	uint16_t reg;

	reg = asicregister_read16(GD_MCFG_GDCFG);

	/// Disable auto programmer
	asicregister_write16(GD_MCFG_GDCFG, (reg & ~(0x20)));  // IOPBASE + 01c0h
	dptx_cfg_phy(cLink);
	dptx_train(cLink, cLanes);
	//Auto Programmer bit will be set in dptx_train on successful link training.
}
/************************************************************************************************
 *  Description:
 *     Debug command handler for DP AUX bus access.
 *
 *  Args:
 *     ENUM_CMD_DPDBG - Specific debug command for DP.
 *
 *  Note:
 *     The intent of this function is to keep access to the AUX bus abstracted.
 *
 ************************************************************************************************/
void dpdbg_cmd(const ENUM_CMD_DPDBG cmd)
{
	switch (cmd) {
	case DP_DBG_DPCD:
		dpdbg_print_dpcd();
	break;
	case DP_DBG_LINK_5_4:
		force_phy(DPCD_LINK_RATE_5_4, 2);
	break;
	case DP_DBG_LINK_2_7:
		force_phy(DPCD_LINK_RATE_2_7, 2);
	break;
	case DP_DBG_LINK_1_62:
		force_phy(DPCD_LINK_RATE_1_62, 2);
	break;
	//case DP_DBG_VPD_STAT:
		//dp_vpd_statout();
	//break;
	default:
		pr_err("Unknown Test");
	break;
	}
}

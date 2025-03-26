// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include "vf111tx.h"

#include "dprx.h"
#include "dptx.h"
#include "dptxaux.h"
#include "edid.h"
#include "dpcd.h"
#include "dpdbg.h"

#define DP_TX_R  0xc0020000 //DisplayPort Source (TX) Registers base address
#define DP_RX_R  0xc0021000 //DisplayPort Source (RX) Registers base address
#define GD_R     0xc00001c0 //Global Display Registers base address
#define V_EDID_R 0x801f0000 //Virtual EDID/Virtual EDID Enable base address

#define AUX_HPD_PIN_ACTIVE ((0x00000001 & asicregister_read32(VF111TX_AUX_STATE)) == 1)
#define AUX_REQ_IN_FLIGHT  ((0x00000002 & asicregister_read32(VF111TX_AUX_STATE)) == 2)
#define AUX_REPLY_RX       ((0x00000004 & asicregister_read32(VF111TX_AUX_STATE)) == 4)
#define AUX_REPLY_TIMEOUT  ((0x00000008 & asicregister_read32(VF111TX_AUX_STATE)) == 8)

#define AUX_HPD_PIN_CONNECTED ((0x00000001 & asicregister_read32(VF111TX_HPD_INPUT_STATE)) == 1)

char *r_dptx_base;
char *r_dprx_base;
char *r_iop_base;
char *r_gd_base;
char *r_v_edid_base;

#define DP_TX_BASE   (0xc0020000)
#define DP_RX_BASE   (0xc0021000)
#define DP_IOP_BASE  (0xc0000000)
#define DP_EDID_BASE (0x801f0000)

int tps3_supported;
int link_training_success_flag;

// Semaphores for DP interrupts and interface access.
struct semaphore gHPDSignal;
struct semaphore gHPDSignal_IRQ;
struct semaphore gHPDSignal_HPD;
struct semaphore gReply;

#define DUMP_REGISTER32(x) pr_info("%8lx:%8lx:"#x"\n", (unsigned long)(x), asicregister_read32((x)))
#define DUMP_REGISTER16(x) pr_info("%8lx:%8x:"#x"\n", (unsigned long)(x), asicregister_read16((x)))
#define DUMP_REGISTER8(x) pr_info("%8lx:%8x:"#x"\n", (unsigned long)(x), asicregister_read8((x)))

#define DUMP_INIT() \
DUMP_REGISTER32(VF111TX_CORE_ID)\
DUMP_REGISTER32(VF111TX_INTERRUPT_MASK)\
DUMP_REGISTER32(VF111TX_AUX_CLOCK_DIVIDER)\
DUMP_REGISTER32(VF111TX_LINK_BW_SET)\

#define DUMP_AUX() \
DUMP_REGISTER32(VF111TX_AUX_CMD)\
DUMP_REGISTER32(VF111TX_AUX_WRITE_FIFO)\
DUMP_REGISTER32(VF111TX_AUX_ADDR)\
DUMP_REGISTER32(VF111TX_AUX_CLOCK_DIVIDER)\
DUMP_REGISTER32(VF111TX_AUX_STATE)\
DUMP_REGISTER32(VF111TX_AUX_REPLY_CODE)\
DUMP_REGISTER32(VF111TX_AUX_REPLY_COUNT)\
DUMP_REGISTER32(VF111TX_INTERRUPT_STATUS)\
DUMP_REGISTER32(VF111TX_INTERRUPT_MASK)\
DUMP_REGISTER32(VF111TX_REPLY_DATA_COUNT)\
DUMP_REGISTER32(VF111TX_AUX_STATUS)\
DUMP_REGISTER32(VF111TX_AUX_REPLY_CLK_WIDTH)\

#define DUMP_DEI() \
DUMP_REGISTER16(GD_MCFG_GDCFG)\
DUMP_REGISTER16(GD_MCTL_GDCTRL)\
DUMP_REGISTER16(GD_ESTAT_GDESR)\
DUMP_REGISTER32(GD_EM_GDEIMR)\
DUMP_REGISTER32(DMCTL_DECR)\
DUMP_REGISTER32(DESTAT_DESR)\
DUMP_REGISTER32(DEINTM_DEIMR)\
DUMP_REGISTER8(VEDIDCTL_VEDIDEN)\
DUMP_REGISTER32(SVGAF_VGAFAULT)\
DUMP_REGISTER32(VRM_VIDRMSK)\


/**********************************************************************************************************
 *  Description:
 *     Enable the Interrupt for DP
 *
 **********************************************************************************************************/
static int enable_interrupt(void)
{
	return 0;
}

/**********************************************************************************************************
 *  Description:
 *     Blocks until an interrupt is received.
 *
 **********************************************************************************************************/
//static void wait_for_interrupt(void)
void dptx_int_hdlr(void)
{
	uint32_t reg_dp_tx;
	uint32_t reg;
	uint32_t bit = 0;

	enable_interrupt();

	/// \todo This should all be a part of a seperate file which supports this transmitter
	reg = asicregister_read32(GD_ESTAT_GDESR);

	if (reg & DP_TX_INT) {

		/// \brief Clear interrupt @ source by reading the register
		///        Determine the interrupts received; Mask off all other bits
		///        which do not matter
		reg_dp_tx = (0x0000000F & asicregister_read32(VF111TX_INTERRUPT_STATUS));
		pr_debug("DP: ISR reg_dp_tx=%d\n", reg_dp_tx);
		/// \note Max of 4 interrupts supported for this TX

		while (reg_dp_tx != 0) {
			if (reg_dp_tx & 0x01) {
				// Interrupt is set; process the respective
				switch (bit) {
				case 0:
					up(&gHPDSignal_HPD);
					up(&gHPDSignal);
					pr_debug("DP: DEBUGINFO: gHPDSignal_HPD Interrupt detected\n");
				break;
				case 1:
					up(&gHPDSignal_IRQ);
					up(&gHPDSignal);
					pr_debug("DP: DEBUGINFO: gHPDSignal_IRQ Interrupt detected\n");
				break;
				case 2:
					up(&gReply);
					pr_debug("DP: DEBUGINFO: gReply Interrupt detected\n");
				case 3:
					// Ignore Interrupt
				break;
				default:
					pr_err("DP: FATAL: Unknown Interrupt %d:0x%x\n", bit, reg_dp_tx);
				break;
				}
			}
			// Increment to next bit
			bit++;
			reg_dp_tx >>= 1;
		}
	}
	if (reg & (SRCDETECT | SRCCHGDONEEVT)) {
		/// \brief Clear interrupts
		asicregister_write32(GD_ESTAT_GDESR, reg | (SRCDETECT | SRCCHGDONEEVT));
		pr_debug("DP: DEBUGINFO:  SRCDETECT SRCCHGDONEEVT Interrupt:0x%x detected\n", reg);
	}
}

void clear_intr(void)
{
	uint32_t reg;

	reg = asicregister_read32(GD_ESTAT_GDESR);
	if (reg & (SRCDETECT | SRCCHGDONEEVT)) {
		// Clear event interrupts
		asicregister_write32(GD_ESTAT_GDESR, reg | (SRCDETECT | SRCCHGDONEEVT));
	}

	// Clear interrupt @ source before enable the interrupt
	asicregister_read32(VF111TX_INTERRUPT_STATUS);
}

static void dpcd_power(uint8_t cVal)
{
	int    i;
	uint32_t retv;

	/// Attempt to wake monitor up a max of 2 times.
	for (i = 0; i < 2; i++) {
		retv = dptxaux_write(DPCD_SET_POWER, 0x01, &cVal);
		if (retv == 0)
			break;

		pr_err("FATAL: Unable to power display: 0x%x\n", retv);
		/// Wait 1 ms and try again; give time for the display to wake up.
		//usleep(1*1000);
		udelay(1*1000);
	}
}

/**********************************************************************************************************
 *  Description:
 *     Debounce the HPD interrupt.
 *
 **********************************************************************************************************/
void debounce_hpd(void)
{
	uint32_t done = 0;

	/// \brief Just received an interrupt for state modification
	///        Initially, wait 1 ms and then check if another interrupt
	///        is pending.
	//usleep(1*1000);
	udelay(1*1000);
	while (!done) {
		/// Clear any new interrupts
		if (down_trylock(&gHPDSignal) == 0) {
			down_trylock(&gHPDSignal_HPD);
			down_trylock(&gHPDSignal_IRQ);
			/// \note More interrupts have occurred; clear the pending interrupts
			/// \todo Wait here until connection stops bouncing
			//usleep(2*100);
			udelay(2*100);
		} else {
			done = 1;
		}
	}
}

/**********************************************************************************************************
 *  Description:
 *     Initializes the API for DisplayPort interaction
 *
 **********************************************************************************************************/
static void main_init(const uint8_t cState)
{
	uint32_t reg;

	/// \brief Enable DisplayPort TX interrupts.
	reg = asicregister_read32(GD_EM_GDEIMR);
	/// \note  Enable the interrupts for the DisplayPort TX (IOP)
	///        Read/Write/Modify
	///        Bit 6  - DP_TX_INT
	/// \note The following interrupts are part of "work around"
	/// \todo Isolate these interrupts to dpwa1.c
	///        bit 15 - SrcChgDoneEn
	///        bit 9  - SrcDetect
	asicregister_write32(GD_EM_GDEIMR, (reg | DP_TX_INT));
}

/**********************************************************************************************************
 *  Description:
 *     Main API for DisplayPort Initialization
 *
 **********************************************************************************************************/
int dp_init(void)
{
	uint8_t  link_rate;
	uint16_t reg;

	r_dptx_base = ioremap(DP_TX_BASE, 0x1000);
	if (r_dptx_base == NULL) {
		pr_err("DP: r_dptx_base is not mapped, failed\n");
		return -1;
	}
	pr_info("DP: r_dptx_base is mapped successfully\n");

	r_dprx_base = ioremap(DP_RX_BASE, 0x1000);
	if (r_dprx_base == NULL) {
		pr_err("DP: r_dprx_base is not mapped, failed\n");
		return -1;
	}
	pr_info("DP: r_dprx_base is mapped successfully\n");

	r_iop_base = ioremap(DP_IOP_BASE, 1024*1024);
	if (r_iop_base == NULL) {
		pr_err("DP: r_iop_base is not mapped, failed\n");
		return -1;
	}
	pr_info("DP: r_iop_base is mapped successfully\n");
	r_gd_base = r_iop_base + 0x1c0;

	r_v_edid_base = ioremap(DP_EDID_BASE, 1024*1024);
	if (r_v_edid_base == NULL) {
		pr_err("DP: r_v_edid_base is not mapped, failed\n");
		return -1;
	}
	pr_info("DP: r_v_edid_base is mapped successfully\n");

	//DUMP_AUX();
	pr_debug("DP: main_init\n");
	main_init(AUX_HPD_PIN_CONNECTED);
	//DUMP_DEI();
	pr_debug("DP: dptx_init\n");
	dptx_init(&gReply, &link_rate);
	//DUMP_AUX();
	//DUMP_INIT();
	pr_debug("DP: dprx_init\n");
	dprx_init();

	pr_debug("DP: DP is ready to connect\n");
	//Enable semaphore debug print
	//SysUtilsDebugSems = 1;
	sema_init(&gHPDSignal, 0);
	sema_init(&gHPDSignal_IRQ, 0);
	sema_init(&gHPDSignal_HPD, 0);
	sema_init(&gReply, 0);

	if (AUX_HPD_PIN_CONNECTED) {
		pr_debug("DP: AUX_HPD_PIN_CONNECTED detected\n");
		up(&gHPDSignal_HPD);
		up(&gHPDSignal);
	}

	return 0;
}

void hotplug_func(void)
{
	uint32_t            retv = 0;
	static              edid_t g_edid;
	edid_t              *pEdid = &g_edid;
	uint8_t             *pBuf;
	uint8_t             buf[sizeof(edid_t)];
	dpcd_rx_capabilty_t link_cap;
	uint16_t reg;

	pr_debug("DP: Entered hotplug function\n");

	for (;;) {
		//pr_debug("%s:%d fn:%s in for-1\n", __FILE__, __LINE__, __func__);
		// Make sure interrupts are enabled
		if (asicregister_read32(VF111TX_INTERRUPT_MASK) != 0x0C)
			asicregister_write32(VF111TX_INTERRUPT_MASK, 0x0C);

		down(&gHPDSignal);
		//pr_debug("%s:%d fn:%s in for after sem wait\n", __FILE__, __LINE__, __func__);
		if (AUX_HPD_PIN_CONNECTED) {
			if (down_trylock(&gHPDSignal_HPD) == 0) {
				int i;
				//Debounce the HPD interrupt
				debounce_hpd();
				pr_debug("\nDP: DP connected\n");
				dpcd_power(1);
				pr_debug("\nDP: Power on the monitor\n");

				retv = edid_read_dp((uint8_t *)pEdid, sizeof(edid_t));
				//fprintf(stderr, "INFO: read EDID retv=0x%x\n", retv);
				pr_debug("DP: INFO: read EDID retv=0x%x\n", retv);

				if (retv != 0) {
					pr_err("DP: WARNING: Failed to read EDID retv=0x%x\n", retv);
					//pEdid = (edid_t*)g_edid_generic;//TODO: Add back later
				} //TODO: Check later }else{
				pBuf = (uint8_t *)pEdid;
				for (i = 0; i < sizeof(edid_t); i++) {
					//fprintf(stderr, "0x%02x,",pBuf[i]);
					//pr_debug("0x%02x,",pBuf[i]);//TODO: Add back later
					if ((i+1)%16 == 0) {
						//fprintf(stderr, "\n");
						//pr_debug("\n");//TODO: Add back later
					}
				}
				//TODO: Check later}
				/// Write/enable virtual EDID
				asicregister_write8(VEDIDCTL_VEDIDEN, 0x0);
				edid_write_virt((uint8_t *)pEdid, sizeof(edid_t));
				asicregister_write8(VEDIDCTL_VEDIDEN, 0x01);
				pr_debug("\nDP: DUMP Virtual EDID\n");
				edid_read_virt(buf, sizeof(edid_t));
				for (i = 0; i < sizeof(edid_t); i++) {
					//fprintf(stderr, "0x%02x,",buf[i]);
					//pr_debug("0x%02x,",buf[i]);//TODO: Add back later
					if ((i+1)%16 == 0) {
						//fprintf(stderr, "\n");
						//pr_debug("\n");//TODO: Add back later
					}
				}
			}

			if (down_trylock(&gHPDSignal_IRQ) == 0)
				pr_debug("\nDP: HPD IRQ received: Re-train the link\n");

			//fprintf(stderr,"\nDUMP DPCD registers before training\n");
			//dpdbg_cmd(DP_DBG_DPCD);
			/// Read the DPCD from the connected device
			retv = dptxaux_read(DPCD_REV, 0x0c, (uint8_t *)&link_cap);

			//Check for TPS3 suppported display
			if ((link_cap.dpcd_rev == 0x12) && (link_cap.max_lane_cnt & 0x40)) {
				tps3_supported = 1;
				pr_debug("DP: tps3 supported DPCD Rev = 0x%x max_lane_cnt = 0x%x\n",
						link_cap.dpcd_rev, link_cap.max_lane_cnt);
			} else {
				tps3_supported = 0;
				pr_debug("DP: tps3 not supported DPCD Rev = 0x%x max_lane_cnt = 0x%x\n",
						link_cap.dpcd_rev, link_cap.max_lane_cnt);
			}

			// Disable HPD and IRQ interrupts during link configuration
			// Unexpected HPD and IRQ interrupts triggered when SRCDETECT and SRCCHGDONEEVT
			// are enabled and partition is powered on.
			asicregister_write32(VF111TX_INTERRUPT_MASK, 0x03);
			/// Compare the capabilities and setup link configuration
			if (retv == 0) {
				link_training_success_flag = 0;
				/// Set link rate
				if (link_cap.max_link_rate >= DPCD_LINK_RATE_5_4) {
					pr_debug("\nDP: DEBUG: DP_DBG_LINK_5_4\n");
					common_phy_link_speed_set(DP_DBG_LINK_5_4);
					dpdbg_cmd(DP_DBG_LINK_5_4);
				} else if (link_cap.max_link_rate >= DPCD_LINK_RATE_2_7) {
					pr_debug("\nDP: DEBUG: DP_DBG_LINK_2_7\n");
					common_phy_link_speed_set(DP_DBG_LINK_2_7);
					dpdbg_cmd(DP_DBG_LINK_2_7);
				} else {
					pr_debug("\nDP: DEBUG: DP_DBG_LINK_1_62\n");
					common_phy_link_speed_set(DP_DBG_LINK_1_62);
					dpdbg_cmd(DP_DBG_LINK_1_62);
				}

				switch (link_cap.max_link_rate) {
				case DPCD_LINK_RATE_5_4:
					if (link_training_success_flag) {
						pr_debug("DP: Link training successful with DP_DBG_LINK_5_4\n");
						break;
					}
					pr_debug("DP: DP_DBG_LINK_5_4 link training failed, trying with DP_DBG_LINK_2_7\n");
					common_phy_link_speed_set(DP_DBG_LINK_2_7);
					dpdbg_cmd(DP_DBG_LINK_2_7);
					fallthrough;
				case DPCD_LINK_RATE_2_7:
					if (link_training_success_flag) {
						pr_debug("DP: Link training successful with DP_DBG_LINK_2_7\n");
						break;
					}
					pr_debug("DP: DP_DBG_LINK_2_7 link training failed, trying with DP_DBG_LINK_1_62\n");
					common_phy_link_speed_set(DP_DBG_LINK_1_62);
					dpdbg_cmd(DP_DBG_LINK_1_62);
					fallthrough;
				case DPCD_LINK_RATE_1_62:
					if (link_training_success_flag) {
						pr_debug("DP: Link training successful with DP_DBG_LINK_1_62\n");
						break;
					}
					pr_debug("DP: DP_DBG_LINK_1_62 link training failed, re-trying with DP_DBG_LINK_1_62\n");
					common_phy_link_speed_set(DP_DBG_LINK_1_62);
					dpdbg_cmd(DP_DBG_LINK_1_62);
					fallthrough;
				default:
					if (!link_training_success_flag)
						pr_err("DP: FATAL: Link training failed\n");
					break;
				}
			} else {
				pr_debug("\nDP: DEBUG: %s: TRY LOWER LINK RATE DP_DBG_LINK_1_62\n", __func__);
				common_phy_link_speed_set(DP_DBG_LINK_1_62);
				dpdbg_cmd(DP_DBG_LINK_1_62);
			}
			// Enable HPD interrupt after link configuration
			asicregister_write32(VF111TX_INTERRUPT_MASK, 0x00);
			//dpdbg_cmd(DP_DBG_DPCD);
			pr_debug("\nDP: %s: DP link training completed\n", __func__);
			//DUMP_DEI()
		} else {
			down_trylock(&gHPDSignal_HPD);
			pr_debug("\nDP: DP disconnected\n");
			reg = asicregister_read16(GD_MCFG_GDCFG);
			/// Disable auto programmer
			asicregister_write16(GD_MCFG_GDCFG, (reg & ~(0x20)));  // IOPBASE + 01c0h
			/// Conduct cleanup of the core registers
			dptx_end();
			/// Disable virtual EDID
			asicregister_write8(VEDIDCTL_VEDIDEN, 0x00);
		}
	}
	pr_debug("DP: Exiting hotplug function\n");
}


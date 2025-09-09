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
#include <asm/io.h>
#include <linux/semaphore.h>

#include "vasregs.h"
#include "vf111tx.h"
#include "dptx.h"
#include "dpcd.h"
#include "dptxaux.h"
//#include "dpwa.h"
#include "dpdbg.h"
#include "common-phy-wrapper-addresses.h"

#define DUMP_REGISTER(x) fprintf(stderr, "%8lx:%8lx:"#x"\n", (long unsigned int)(x), asicregister_read32((x)));

/// \todo put these in a common header file!
#define PHY_PIP_CONFIG_REG_INDEX_ADDR  (0x0fC0)  // RW
#define PHY_PIP_CONFIG_REG_DATA_ADDR   (0x0fC4)  // RW

typedef struct{
    uint8_t *pVswing_Preemph;
    uint8_t vsw_pre_sz;
} serdes_t;


/// \todo: Clean up by creating a 2-D array?
/// \brief Supported voltage swing values identified by Marvell and stated in the VESA spec.
///        400, 500, 600, 800 mV
///        These default values can be updated during initialization if the platform has
///        defined new values.
/* commented out
static uint8_t g_swing_level[] = {COMPHY_SERDES_480MV,COMPHY_SERDES_570MV,COMPHY_SERDES_680MV,COMPHY_SERDES_890MV};
static uint8_t g_swing_level_lbr[] = {COMPHY_SERDES_480MV,COMPHY_SERDES_570MV,COMPHY_SERDES_680MV,COMPHY_SERDES_890MV};
*/
/// \brief Pre-emphasis values identified by Marvell and stated in the VESA spec.
///        0, 3, 6 dB
///        These default values can be updated during initialization if the platform has
///        defined new values.
/* commented out
static uint8_t g_preemphasis[] = {COMPHY_SERDES_0DB,COMPHY_SERDES_3DB,COMPHY_SERDES_6DB};
static uint8_t g_preemphasis_lbr[] = {COMPHY_SERDES_0DB,COMPHY_SERDES_3DB,COMPHY_SERDES_6DB};
*/

static uint8_t g_level_lbr[4*4*2] = {

    /* Level vsing_level = 0 */
    0x4, 0x0, /* Preemph_level = 0 */
    0x3, 0x4, /* Preemph_level = 1 */
    0x1, 0x6, /* Preemph_level = 2 */
    0x0, 0x6, /* Preemph_level = 3 */

    /* Level vsing_level = 1 */
    0x3, 0x0, /* Preemph_level = 0 */
    0x0, 0x4, /* Preemph_level = 1 */
    0x0, 0x6, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 2 */
    0x1, 0x0, /* Preemph_level = 0 */
    0x0, 0x4, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 3 */
    0x0, 0x0, /* Preemph_level = 0 */
    0xFF, 0xFF, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */
};

static uint8_t g_level_hbr[4*4*2] = {

    /* Level vsing_level = 0 */
    0x4, 0x0, /* Preemph_level = 0 */
    0x3, 0x4, /* Preemph_level = 1 */
    0x1, 0x6, /* Preemph_level = 2 */
    0x0, 0x6, /* Preemph_level = 3 */

    /* Level vsing_level = 1 */
    0x3, 0x0, /* Preemph_level = 0 */
    0x0, 0x4, /* Preemph_level = 1 */
    0x0, 0x6, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 2 */
    0x1, 0x0, /* Preemph_level = 0 */
    0x0, 0x4, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 3 */
    0x0, 0x0, /* Preemph_level = 0 */
    0xFF, 0xFF, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */
};

static uint8_t g_level_hbr2[4*4*2] = {

    /* Level vsing_level = 0 */
    0x4, 0x0, /* Preemph_level = 0 */
    0x3, 0x3, /* Preemph_level = 1 */
    0x1, 0x5, /* Preemph_level = 2 */
    0x0, 0x6, /* Preemph_level = 3 */

    /* Level vsing_level = 1 */
    0x3, 0x0, /* Preemph_level = 0 */
    0x0, 0x3, /* Preemph_level = 1 */
    0x0, 0x5, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 2 */
    0x1, 0x0, /* Preemph_level = 0 */
    0x0, 0x3, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */

    /* Level vsing_level = 3 */
    0x0, 0x0, /* Preemph_level = 0 */
    0xFF, 0xFF, /* Preemph_level = 1 */
    0xFF, 0xFF, /* Preemph_level = 2 */
    0xFF, 0xFF, /* Preemph_level = 3 */
};

static int common_phy_register_write(unsigned int reg, unsigned int data)
{
    //TODO: Change this later to common phy ioctl write call

    asicregister_write32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_INDEX_ADDR, reg);
    asicregister_write32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_DATA_ADDR, data);

	pr_debug("Write Phy reg=0x%x, data=0x%x\n", reg, data);

    return 0;
}

static int common_phy_register_read(unsigned int reg, unsigned int *data)
{
    //TODO: Change this later to common phy read call

    //dummy test read
	unsigned int test_read __maybe_unused = 0;

    asicregister_write32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_INDEX_ADDR, 0x80f8);
    test_read = asicregister_read32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_DATA_ADDR);

    asicregister_write32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_INDEX_ADDR, reg);
    *data = asicregister_read32(R_DPTX_BASE+PHY_PIP_CONFIG_REG_DATA_ADDR);

	pr_debug("Read Phy reg=0x%x, data=0x%x\n", reg, *data);

    return 0;
}

/*
 * Polling the register for the right value.
 */
static int poll_register(unsigned int reg, int value)
{
	int data = 0;
	int retry_cnt = 20;

	while (retry_cnt > 0) {
		common_phy_register_read(reg, &data);
		if (data == value) {
			//poll matched, received the required value
			return 0;
		}
		//usleep(1000);
		udelay(1000);
		retry_cnt--;
	}

	//poll failed, did not receive the required value
	pr_err("%s: reg=0x%x expected_value=0x%x current_value=0x%x polling failed\n",
		__func__, reg, value, data);
	return -1;
}

/*
 * Set Phy Link Speed and Lane Count Setting.
 */
int common_phy_link_speed_set(int link_rate)
{
    int xcvr_mode_lane0 = 0;
    int phy_dp_link_rate  = 0;
    unsigned int reg_data = 0;
    int retv = 0;

	pr_debug("Set DP link speed for link_rate=%d\n", link_rate);

    if (link_rate == DP_DBG_LINK_5_4) {
	xcvr_mode_lane0 = 2;
	phy_dp_link_rate = DPCD_LINK_RATE_5_4;
    } else if (link_rate == DP_DBG_LINK_2_7) {
	xcvr_mode_lane0 = 1;
	phy_dp_link_rate = DPCD_LINK_RATE_2_7;
    } else if (link_rate == DP_DBG_LINK_1_62) {
	xcvr_mode_lane0 = 0;
	phy_dp_link_rate = DPCD_LINK_RATE_1_62;
    } else {
	pr_err("Invalid link_rate passed as argument %d\n", link_rate);
	return -1;
    }

    /* 2. Verify that initial PHY lane settings are complete, bit0 and bit1 set to 1 */
    common_phy_register_read(kWRAP_PHY_DP_LANESET_READY, &reg_data);
    reg_data =  reg_data | 0x03;
    if (poll_register(kWRAP_PHY_DP_LANESET_READY, reg_data) != 0) {
	pr_err("%s: kWRAP_PHY_DP_LANESET_READY polling failed\n", __func__);
	retv = -1;
    }

    /* 3. Clear PHY data enable */
    common_phy_register_write(kWRAP_PHY_TX_DATA_ENABLE, 0x00);
    common_phy_register_write(kWRAP_PHY_RX_DATA_ENABLE, 0x00);

    /* 4. Request rate change */
    common_phy_register_write(kWRAP_XCVR_RATE_CHNG0_REQ, 0x01);

    /* 5. Poll for rate change ACK */
    if (poll_register(kWRAP_XCVR_RATE_CHNG0_ACK, 0x01) != 0) {
	pr_err("%s: kWRAP_XCVR_RATE_CHNG0_ACK polling failed\n", __func__);
	retv = -1;
    }

    /* 6. Clear PLL clock enable */
    common_phy_register_write(kWRAP_XCVR_PLLCLK0_ENABLE, 0x00);

    /* 7. Poll for clear of PLL clock enable ACK */
    if (poll_register(kWRAP_XCVR_PLLCLK0_EN_ACK, 0x00) != 0) {
	pr_err("%s: kWRAP_XCVR_PLLCLK0_EN_ACK polling failed\n", __func__);
	retv = -1;
    }

    /* 9. Change transceiver link rate */
    common_phy_register_write(kWRAP_XCVR_MODE_LANE0, xcvr_mode_lane0);

    /* 10. Set PLL clock enable */
    common_phy_register_write(kWRAP_XCVR_PLLCLK0_ENABLE, 0x01);

    /* 11. Poll Common ready to ensure common block is still functional */
    if (poll_register(kWRAP_PHY_CMN_READY, 0x01) != 0) {
	pr_err("%s: kWRAP_PHY_CMN_READY polling failed\n", __func__);
	retv = -1;
    }

    /* 12. Poll for PLL clock enable ACK */
    if (poll_register(kWRAP_XCVR_PLLCLK0_EN_ACK, 0x01) != 0) {
	pr_err("%s: kWRAP_XCVR_PLLCLK0_EN_ACK polling failed\n", __func__);
	retv = -1;
    }

    /* 13. Release rate change request */
    common_phy_register_write(kWRAP_XCVR_RATE_CHNG0_REQ, 0x00);

    /* 14. Poll for rate change ACK clear */
    if (poll_register(kWRAP_XCVR_RATE_CHNG0_ACK, 0x00) != 0) {
	pr_err("%s: kWRAP_XCVR_RATE_CHNG0_ACK failed\n", __func__);
	retv = -1;
    }

    /* 15. Poll for lane set ready, bit0 and bit1 should be set to 1 */
    common_phy_register_read(kWRAP_PHY_DP_LANESET_READY, &reg_data);
    reg_data =  reg_data | 0x03;
    if (poll_register(kWRAP_PHY_DP_LANESET_READY, reg_data) != 0) {
	pr_err("%s: kWRAP_PHY_DP_LANESET_READY failed\n", __func__);
	retv = -1;
    }

    /* 16. Enable PHY data */
    common_phy_register_write(kWRAP_PHY_TX_DATA_ENABLE, 0x01);
    common_phy_register_write(kWRAP_PHY_RX_DATA_ENABLE, 0x01);

    /* 17. Set wrapper PHY link rate */
    common_phy_register_write(kWRAP_PHY_DP_LINK_RATE, phy_dp_link_rate);

    return retv;
}


static serdes_t g_serdes_cfg;
/**********************************************************************************************************
*  Description:
*     Train the DP port with a specific pattern.
*
*  Args:
*     uint32_t - Delay prior to read.
*     uint8_t  - Pattern.
*
*  Return:
*     uint8_t - Status bits defined in VESA Spec.
*
*  Note:
*     The correct lane count and bandwidth should be set prior to calling this function.
*
*  TODO:
*     The delay may or may not be needed.  Early testing showed mixed results with aux channel
*     reads so the delay was added.
*
**********************************************************************************************************/
static uint8_t dptx_train_pattern(uint32_t delay, uint8_t pat)
{
	uint32_t retv;
	uint8_t  status;

	//usleep(delay);
	udelay(delay);
	retv = dptxaux_read(DPCD_LANE0_1_STATUS, 1, &status);
	if (retv) {
		pr_err("%s: Failed to read [R03%xh]\n", __func__, DPCD_LANE0_1_STATUS);
		status = 0;
	}

	return (status);
}

/**********************************************************************************************************
*  Description:
*     Adjust voltage swing and pre-emphasis based on DPCD status registers.
*
*  Args:
*     uint16_t - size of vswing settings array
*     uint16_t - size of pre-emphasis settings array
*     uint8_t* - pointer to vswing settings array
*     uint8_t* - pointer to emphasis settings array
*
*  Return:
*     uint32_t - Status of aux transaction
*
*  Note:
*
**********************************************************************************************************/
static uint32_t adjust_vsw_preemphasis(const uint16_t cVsz, const uint16_t cPsz, uint8_t *p_g_level, uint8_t *pPre, uint8_t link_rate)
{
	uint32_t retv;
	uint32_t lane_vswing[2];
	uint32_t lane_preem[2];
	uint8_t  status;
	uint8_t  swing[2];
	uint8_t  preem[2];
	uint32_t  curr_pre[2], next_pre[2];
	uint32_t  curr[2], next[2];
	uint8_t bit_shift = 0;

	if (link_rate == DPCD_LINK_RATE_5_4) {
		bit_shift = 16;
	} else if (link_rate == DPCD_LINK_RATE_2_7) {
		bit_shift = 8;
	} else {
		bit_shift = 0;
	}

	pr_debug("In %s.... bit_shift=%d\n", __func__, bit_shift);

	/// Training failed; determine what should be adjusted.
	retv = dptxaux_read(DPCD_ADJ_REQ_LANE0_1, 1, &status);
	if (!retv) {
		/// Have the status, what needs to be adjusted
		swing[0] = (status & 0x3);
		preem[0] = (status & 0xc)>>2;
		swing[1] = (status & 0x30)>>4;
		preem[1] = (status & 0xc0)>>6;

		//print swing, preem count level
		pr_debug("%s: level count : lane0 swing=0x%x preemph=0x%x, lane1 swing=0x%x preemph=0x%x\n",
				__func__, swing[0], preem[0],  swing[1], preem[1]);

		// For lane 0
		common_phy_register_read(0x8090, &lane_vswing[0]);
		common_phy_register_read(0x8080, &lane_preem[0]);

		// For lane 1
		common_phy_register_read(0x8094, &lane_vswing[1]);
		common_phy_register_read(0x8084, &lane_preem[1]);

		/// Determine current setting
		curr[0]     = lane_vswing[0];//((lane[0]>>1) & 0x1f);
		curr_pre[0] = lane_preem[0];//((lane[0]>>7) & 0xf);

		curr[1]     = lane_vswing[1];//((lane[1]>>1) & 0x1f);
		curr_pre[1] = lane_preem[1];//((lane[1]>>7) & 0xf);

		/// Clear bits for voltage swing setting and pre-emphasis
		//lane[0] &= 0xf841;
		//lane[1] &= 0xf841;

		pr_debug("vswing0 index=%d, preem0 index=%d, vswing1=%d, preem1 index=%d\n",
			(swing[0]*8 + preem[0]*2 + 0), (swing[0]*8 + preem[0]*2 + 1),
			(swing[1]*8 + preem[1]*2 + 0), (swing[1]*8 + preem[1]*2 + 1));

		next[0] = p_g_level[swing[0]*8 + preem[0]*2 + 0];
		next_pre[0] = p_g_level[swing[0]*8 + preem[0]*2 + 1];

		next[1] = p_g_level[swing[1]*8 + preem[1]*2 + 0];
		next_pre[1] = p_g_level[swing[1]*8 + preem[1]*2 + 1];

		if ((next[0] == 0xFF) ||
			(next_pre[0] == 0xFF) ||
			(next[1] == 0xFF) ||
			(next_pre[1] == 0xFF)) {
			pr_err("Invalid vswing or pre-emp values 0xFF next[0]=0x%x, next_pre[0]=0x%x "
				"next[1]=0x%x, next_pre[1]=0x%x\n",
				next[0], next_pre[0], next[1], next_pre[1]);
			retv = 3;
			goto skip_swing_conf;

		}

		next[0] = ((curr[0] & ~(0x7 << bit_shift)) | (next[0] << bit_shift));
		next_pre[0] = ((curr_pre[0] & ~(0x7 << bit_shift)) | (next_pre[0] << bit_shift));

		next[1] = ((curr[1] & ~(0x7 << bit_shift)) | (next[1] << bit_shift));
		next_pre[1] = ((curr_pre[1] & ~(0x7 << bit_shift)) | (next_pre[1] << bit_shift));

		//print swing, preem, next, lane
		pr_debug("%s: current values: lane0 vswing=0x%x preemph=0x%x, lane1 vswing=0x%x preemph=0x%x\n",
				__func__, curr[0], curr_pre[0], curr[1], curr_pre[1]);
		pr_debug("%s: next values: lane0 vswing=0x%x preemph=0x%x, lane1 vswing=0x%x preemph=0x%x\n",
				__func__, next[0], next_pre[0], next[1], next_pre[1]);

		/// Write current voltage swing & pre-emphasis
		// For lane 0
		common_phy_register_read(0x8090, &lane_vswing[0]);
		common_phy_register_read(0x8080, &lane_preem[0]);

		// For lane 1
		common_phy_register_read(0x8094, &lane_vswing[1]);
		common_phy_register_read(0x8084, &lane_preem[1]);

		/// For lane 0
		common_phy_register_write(0x8090, next[0]);
		common_phy_register_write(0x8080, next_pre[0]);

		/// Write current voltage swing & pre-emphasis
		/// For lane 1
		common_phy_register_write(0x8094, next[1]);
		common_phy_register_write(0x8084, next_pre[1]);

		status = 0;
		if (swing[0] >= 3) {
			// Max voltage swing level; set bit 2
			status |= 0x04;
		}
		status |= swing[0];
		status |= (preem[0]<<3);

		if (preem[0] >= 3) {
			status |= 0x20;
		} else {
			//Check if next vswing/preem value is 0xFF
			if (p_g_level[swing[0]*8 + preem[0]*2 + 1 + 1] == 0xFF) {
				status |= 0x20;
			}
		}
		swing[0] = status;

		status = 0;
		if (swing[1] >= 3) {
			// Max voltage swing level; set bit 2
			status |= 0x04;
		}
		status |= swing[1];
		status |= (preem[1]<<3);

		if (preem[1] >= 3) {
			status |= 0x20;
		} else {
			//Check if next vswing/preem value is 0xFF
			if (p_g_level[swing[1]*8 + preem[1]*2 + 1 + 1] == 0xFF) {
			status |= 0x20;
		}
	}

	swing[1] = status;

	/// Write both updated value to TRAINING_LANEx_SET
	retv = dptxaux_write(DPCD_TRAINING_LANE0_SET, 2, swing);

	if ((next[0] == curr[0]) && (next_pre[0] == curr_pre[0])) {
		retv = 2;
	}
	} else {
		pr_err("WARNING: %s: Failed to read [R03%xh]\n", __func__, DPCD_ADJ_REQ_LANE0_1);
	}

skip_swing_conf:
	return (retv);
}

/**********************************************************************************************************
*  Description:
*     Write bandwidth being used.
*
*  Args:
*     uint8_t    -  max lanes with enhanced framing
*     uint8_t    -  link speed for dpcd
*
*  Return:
*
*  Note:
*
**********************************************************************************************************/
/*static unit32_t bw(const uint8_t cLaneMax, const uint8_t cLink)
{
    uint32_t retv;
    uint8_t val[2];

    asicregister_write32(VF111TX_ENHANCED_FRAME_EN, (cLaneMax & 0x80)>>7);
    val[0] = cLink;
    val[1] = cLaneMax;
    /// Write training pattern
    retv = dptxaux_write(DPCD_LINK_BW_SET,sizeof(val),val);
    return retv;
}*/

/**********************************************************************************************************
*  Description:
*     Implement training sequence 1.
*
*  Args:
*     uint8_t    -  max lanes with enhanced framing
*     uint8_t    -  link speed for dpcd
*     serdes_t - serdes settings
*
*  Return:
*
*  Note:
*
**********************************************************************************************************/
static uint32_t ts1(serdes_t *pCfg, uint8_t link_rate)
{
	uint32_t done = 0;
	volatile uint32_t retv;
	uint8_t val[5];
	uint8_t cnt;
	uint8_t pattern = 1;
	uint32_t lane_vswing[2];
	uint32_t lane_preem[2];

	cnt = 0;
	val[0] = 0x20 | pattern;
	val[1] = 0;
	val[2] = 0;
	val[3] = 0;
	val[4] = 0;

	/// write to source device (must preceed the write to the sink by specification 3.5.1.3)
	asicregister_write32(VF111TX_TRAINING_PATTERN_SET, pattern);

	/// Write training pattern
	retv = dptxaux_write(DPCD_TRAINING_PAT_SET, sizeof(val), val);

	if (retv != DPTX_AUX_NATIVE_ACK) {
		pr_err("WARNING: %s: Write Failed 0x%02x\n", __func__, retv);
	}

	/// clock recovery
	cnt = 0;
	while (!done) {

		retv = dptx_train_pattern(200, pattern);
		pr_debug("%s: after calling dptx_train_pattern 0x%x\n", __func__, retv);
		/// \note training pattern returned for 2 lanes! 0x11
		if (!(retv & 0x11)) {
			retv = adjust_vsw_preemphasis(pCfg->vsw_pre_sz, pCfg->vsw_pre_sz, pCfg->pVswing_Preemph, pCfg->pVswing_Preemph, link_rate);
			if (retv != 0) {
				//break;
				if (cnt < 5) {
					cnt++;
				} else {
					retv = cnt;
					break;
				}
			}
		} else {
			// For lane 0
			common_phy_register_read(0x8090, &lane_vswing[0]);
			common_phy_register_read(0x8080, &lane_preem[0]);

			// For lane 1
			common_phy_register_read(0x8094, &lane_vswing[1]);
			common_phy_register_read(0x8084, &lane_preem[1]);

			pr_debug("INFO: %s: Training passed lane0 vswing=0x%x preem=0x%x, lane1 vswing=0x%x preem=0x%x\n",
					__func__, lane_vswing[0], lane_preem[0], lane_vswing[1], lane_preem[1]);

			retv = 0;
			break;
		}
	}
	return (retv);
}

/**********************************************************************************************************
*  Description:
*     Implement training sequence 2.
*
*  Args:
*     serdes_t - serdes settings
*
*  Note:
*     Training sequence 3 is preferred if supported.
*     TODO: Need to add support to validate if TS3 exists.
*
**********************************************************************************************************/
static uint32_t ts2(serdes_t *pCfg, uint8_t link_rate)
{
	uint32_t done = 0;
	volatile uint32_t retv;
	uint8_t cnt;
	uint8_t burst;
	uint8_t pattern;
	uint32_t lane_vswing[2];
	uint32_t lane_preem[2];

	if (tps3_supported) {
		pattern = 3;
	} else {
		pattern = 2;
	}

	burst = 0x20 | pattern;
	/// write to source device (must preceed the write to the sink by specification 3.5.1.3)
	asicregister_write32(VF111TX_TRAINING_PATTERN_SET, pattern);

	retv = dptxaux_write(DPCD_TRAINING_PAT_SET, 1, &burst);
	if (retv != DPTX_AUX_NATIVE_ACK) {
		pr_err("WARNING: %s: Write Failed 0x%02x\n", __func__, retv);
	}

	/// clock recovery
	cnt = 0;
	while (!done) {

		retv = dptx_train_pattern(500, pattern);
		/// \note training pattern returned for 2 lanes! 0x11
		/// \note Validate CR is still in effect
		pr_debug("%s: after calling dptx_train_pattern 0x%x\n", __func__, retv);
		if (retv & 0x11) {
			if (retv != 0x77) {
				retv = adjust_vsw_preemphasis(pCfg->vsw_pre_sz, pCfg->vsw_pre_sz, pCfg->pVswing_Preemph, pCfg->pVswing_Preemph, link_rate);
				if (retv != 0) {
					//break;
					if (cnt < 5) {
						cnt++;
					} else {
						retv = cnt;
						break;
					}
				}
			} else {

				// For lane 0
				common_phy_register_read(0x8090, &lane_vswing[0]);
				common_phy_register_read(0x8080, &lane_preem[0]);

				// For lane 1
				common_phy_register_read(0x8094, &lane_vswing[1]);
				common_phy_register_read(0x8084, &lane_preem[1]);

				pr_debug("INFO: %s: Training passed lane0 vswing=0x%x preem=0x%x, lane1 vswing=0x%x preem=0x%x\n",
					__func__, lane_vswing[0], lane_preem[0], lane_vswing[1], lane_preem[1]);

				pr_debug("INFO: %s: Training passed\n", __func__);
				retv = 0;
				break;
			}
		} else {
			pr_err("WARNING: %s: Need to run CR\n", __func__);
			retv = 2;
			break;
		}
	}
	return retv;
}

/**********************************************************************************************************
*  Description:
*     Train the DP port.
*
**********************************************************************************************************/
uint32_t dptx_train(const uint8_t cLinkRate, const uint8_t max_lane)
{
	uint32_t retv;
	uint32_t  val32 = 0;
	uint16_t  val16 = 0;
	uint8_t  val = 0;
	serdes_t *serdes = &g_serdes_cfg;

	pr_debug("%s: link rate = %d max lane = %d\n", __func__, cLinkRate, max_lane);
	/// \brief Enable transmitter before training. [R080h]
	asicregister_write32(VF111TX_TX_OUTPUT_EN, 0x01);

	/// Disable main stream [R084h]
	asicregister_write32(VF111TX_MAIN_STREAM_EN, 0x00);

	/// Initialize serdes type based on link rate
	if (cLinkRate == DPCD_LINK_RATE_5_4) {
		pr_debug("%s: 5.4G cLinkRate=%d\n", __func__, cLinkRate);
		serdes->pVswing_Preemph  = g_level_hbr2;
	} else if (cLinkRate == DPCD_LINK_RATE_2_7) {
		pr_debug("%s: 2.7G cLinkRate=%d\n", __func__, cLinkRate);
		serdes->pVswing_Preemph  = g_level_hbr;
	} else {
		pr_debug("%s: 1.6G cLinkRate=%d\n", __func__, cLinkRate);
		serdes->pVswing_Preemph  = g_level_lbr;
	}

	/// Initialize serdes to lowest levels
	//marvell_init_serdes(serdes->pVswing[0],serdes->pPreemph[0]);//commented

/* Post DP Tx Init */
	/// Read DPCD Registers - •	Aux Channel Read 0x00 – 0xFF

	/// Set link rate [R000h]
	asicregister_write32(VF111TX_LINK_BW_SET, cLinkRate & 0x1f);
	/// Set lanes being used [R004h]
	asicregister_write32(VF111TX_LANE_COUNT_SET, max_lane & 0x02);//changed from 0x3 to 0x2
	///Aux Channel - Link Speed Rate
	val = cLinkRate & 0x1f;
	retv = dptxaux_write(DPCD_LINK_BW_SET, 1, &val);
	if (retv != 0) {
		pr_debug("WARNING: %s:%d Write Failed DPCD_LINK_BW_SET ret 0x%02x\n",
				__func__, __LINE__, retv);
	}
	///Aux Channel - Lane Count and Enhanced Framing
	val = 0x82;
	retv = dptxaux_write(DPCD_LANE_CNT_SET, 1, &val);
	if (retv != 0) {
		pr_err("WARNING: %s:%d Write Failed DPCD_LANE_CNT_SET ret 0x%02x\n",
			__func__, __LINE__, retv);
	}
	///Aux Channel - Set Sink to D0 (normal operation) Power state
	val = 0x01;
	retv = dptxaux_write(DPCD_SET_POWER, 1, &val);
	if (retv != 0) {
		pr_err("WARNING: %s:%d Write Failed DPCD_SET_POWER ret 0x%02x\n",
			__func__, __LINE__, retv);
	}

/* Link Training Start */

	/// Disable scrambling
	asicregister_write32(VF111TX_SCRAMBLING_DISABLE, 0x01);
	//retv = bw(max_lane,cLinkRate);

	retv = ts1(serdes, cLinkRate);
	if (retv == 0) {
		retv = ts2(serdes, cLinkRate);
		if (retv == 2) {
			/// CR lost; try to gain CR and try again
			if (ts1(serdes, cLinkRate) == 0) {
				retv = ts2(serdes, cLinkRate);
			}
		}
	}

	if (retv) {
		pr_err("DP: WARNING: %s: Implement lower bit rate, retv=%d\n", __func__, retv);
		link_training_success_flag = 0;//Indicate link training failed
	} else {

		/// \brief The auto programmer enables and disables
		///        the main stream based on system power state.
		///        Disabling the main stream will cause the link state to change.
		/// \note
		///        The link needs to stay active when system is powered off to
		///        allow aux video/overlay.

		/// \brief clear up training registers
		asicregister_write32(VF111TX_SCRAMBLING_DISABLE, 0x00);
		asicregister_write32(VF111TX_TRAINING_PATTERN_SET, 0x00);

		//Write INPUT_SOURCE_ENABLE
		asicregister_write32(VF111TX_BASE_ADDRESS + 0x94, 0x01);

		//USER_STREAM_ENABLE_NO_VSYNC
		val32 = asicregister_read32(VF111TX_BASE_ADDRESS + 0x868);//(0xC0020868);
		val32 |= (0x1 << 1);
		asicregister_write32(VF111TX_BASE_ADDRESS + 0x868, val32);//(0xC0020868, val);

		//Enable DPTX Autonomous Program Enable
		val16 = asicregister_read16(GD_MCFG_GDCFG);//(0xC00001C0);
		val16 |= (0x1 << 5);
		asicregister_write16(GD_MCFG_GDCFG, val16);//(0xC00001C0, val);

		val = 0x0;
		retv = dptxaux_write(DPCD_TRAINING_PAT_SET, 1, &val);

		pr_debug("DP: %s: Link training is successful\n", __func__);
		link_training_success_flag = 1;//Indicate link training successful

	}

	return retv;
}

/**********************************************************************************************************
*  Description:
*     Configure SERDES PHY for a particular bandwidth/speed.
*     Recommendation from ASIC team: Always configure both lanes.  The core will
*     determine if one or two lanes will be used.
*
*  Args:
*     uint32_t - Bandwidth/Link-rate to program the phy
*
*  Note:
*     The core will be set to the speed of the PHY after completion.  This will allow the FW to
*     determine if the PHY has been initialized.
*
**********************************************************************************************************/
void dptx_cfg_phy(const uint32_t cBandwidth)//All the initialization relate to phy is moved to common phy driver.
{
#if 0
	uint32_t status;
	uint8_t  cnt = 0;
	uint8_t  cnt_max;

	/// Clear bandwidth in core prior to configuring PHY.
	asicregister_write32(VF111TX_LINK_BW_SET, 0x00);

	/// Set both PHYs for normal operation
	asicregister_write32(VF111TX_PHY_RESET, 0x0);
	asicregister_write32(0x200 | R_DPRX_BASE, 0x0);

	/* Start of link training */
	/// Set both PHYs into reset prior to configuration
	asicregister_write32(VF111TX_PHY_RESET, 0x1);
	asicregister_write32(0x200 | R_DPRX_BASE, 0x1);

	asicregister_write32(VF111TX_PHY_ELEC_IDLE, 0x0f);
	/// Phy power down for TX
	asicregister_write32(VF111TX_PHY_PWR_DOWN, 0x0f);
	/// Phy power down for RX
	asicregister_write32(0x210 | R_DPRX_BASE, 0x0f);

	/// TX reset release except elastic buffer
	asicregister_write32(VF111TX_PHY_RESET, 0x2);
	/// RX reset release except elastic buffer
	//asicregister_write32(0x200 | R_DPRX_BASE,0x8);commented out

	/// \brief Wait for PHY to complete reset process prior to
	///        initializing.
	/// \note  Max wait is a "best" guess based on empirical testing.
	cnt_max = 5;
	do {
		status = (0x03 & asicregister_read32(VF111TX_PHY_STATUS));
		/// \note Bits [0:1] should be set
		if (status == 0x3) {
			fprintf(stderr, "INFO: PHY Reset completed\n");
			break;
		} else {
			fprintf(stderr, "INFO: Waiting on PHY Reset to complete (%d).\n", cnt);
			usleep(400);
			cnt++;
		}
	} while (cnt_max < 5);

	if (cnt == cnt_max) {
		fprintf(stderr, "FATAL: PHY reset incomplete; %04lxh.\n", status);
	}

	if (cBandwidth == DPCD_LINK_RATE_2_7) {
		marvell_init_hbr();
	} else {
		marvell_init_lbr();
	}

	/// Phy Power up TX & RX (Only 2 lanes are used)
	/// Power them all up.
	/// \todo Investigate power up in the future.
	asicregister_write32(VF111TX_PHY_PWR_DOWN, 0x00);
	asicregister_write32(0x210 | R_DPRX_BASE, 0x00);

	/// \brief Calibrate the Phy
	///        After calibration, wait for PLL to lock
	marvell_init_calibrate();
	cnt = 0;
	/// \note  Max wait is a "best" guess based on empirical testing.
	cnt_max = 10;
	do {
		status = (0x30 & asicregister_read32(VF111TX_PHY_STATUS));
		/// \Note Bits [4:5] should be set to show PLL locked
		if (status == 0x30) {
			break;
		} else {
			//fprintf(stderr,"INFO:Waiting on PLL DO(1)(%d)\n",cnt);
			usleep(800);
			cnt++;
		}
	} while (cnt < cnt_max);

	if (cnt == cnt_max) {
		fprintf(stderr, "FATAL: PLL Lock incomplete; %04lxh.\n", status);
	}

	cnt = 0;
	/// \note  Max wait is a "best" guess based on empirical testing.
	cnt_max = 10;
	do {
		status = (0x30 & asicregister_read32(0x208 | R_DPRX_BASE));
		/// \Note Bits [4:5] should be set to show PLL locked
		if (status == 0x30) {
			break;
		} else {
			//fprintf(stderr,"INFO:Waiting on PLL (DO2)(%d)\n",cnt);
			usleep(800);
			cnt++;
		}
	} while (cnt < cnt_max);

	if (cnt == cnt_max) {
		fprintf(stderr, "FATAL: PLL Lock incomplete; %04lxh.\n", status);
	}

	marvell_turn_off_clk();

	/// If LBR, run additional settings for to train phy.
	if (cBandwidth == DPCD_LINK_RATE_1_62) {
		marvell_addtl_steps();
	} else {
		marvell_lane_align_rdy();
	}

	marvell_void_turn_off_clk();
	cnt = 0;
	/// \note  Max wait is a "best" guess based on empirical testing.
	///        Normally the PLL stays locked here.
	cnt_max = 5;
	do {
		status = (0x30 & asicregister_read32(VF111TX_PHY_STATUS));
		/// \Note Bits [4:5] should be set to show PLL locked
		if (status == 0x30) {
			break;
		} else {
			//fprintf(stderr,"INFO:Waiting on PLL DO(3) (%d)\n",cnt);
			usleep(800);
			cnt++;
		}
	} while (cnt < cnt_max);

	if (cnt == cnt_max) {
		fprintf(stderr, "FATAL: PLL Lock incomplete; %04lxh.\n", status);
	}

	/// \brief Set PIN_RX_INIT of RX PHY register 0x238 (undocumented by DP IP Block)
	///        Set bit 10.  Self clearing bit.
	asicregister_write32(0x228 | R_DPRX_BASE, 0x400);
	cnt = 0;
	/// \note  Max wait is a "best" guess based on empirical testing.
	cnt_max = 5;
	do {
		status = (0x30000 & asicregister_read32(0x208 | R_DPRX_BASE));
		/// \Note Bits [17:16] should be set to show RX_INIT_DONE = 1.
		if (status == 0) {
			break;
		} else {
			fprintf(stderr, "FATAL: Waiting on RX to clear (%d)\n", cnt);
			usleep(400);
			cnt++;
		}
	} while (cnt < cnt_max);

	if (cnt == cnt_max) {
		fprintf(stderr, "FATAL: RX_INIT_DONE not set; %04lxh.\n", status);
	}

	/// Set both PHYs for normal operation
	asicregister_write32(VF111TX_PHY_RESET, 0x0);
	asicregister_write32(0x200 | R_DPRX_BASE, 0x0);

	/// PHY init complete, set bandwidwidth in core
	asicregister_write32(VF111TX_LINK_BW_SET, cBandwidth);

	asicregister_write32(VF111TX_PHY_ELEC_IDLE, 0x0);
#endif
}

/**********************************************************************************************************
*  Description:
*     DisplayPort TX connection has ended.  Conduct cleanup of the core registers.
*
*  Args:
*     none
*
*  Return:
*     none
*
*  Note:
*     Currently only disable main link.  There may be other things to disable in the future.
*
**********************************************************************************************************/
void dptx_end(void)
{
    /// \brief Disable transmitter and main stream.
    asicregister_write32(VF111TX_TX_OUTPUT_EN, 0x00);
    asicregister_write32(VF111TX_MAIN_STREAM_EN, 0x00);
}


/**********************************************************************************************************
*  Description:
*     Initialize the DisplayPort TX.
*
*  Args:
*     Semaphore* - pointer to semaphore utilized for callback
*     uint8_t*     - pointer to determine current link rate/bandwidth
*
*  Return:
*     retv > 0 - TX CORE is not configured.
*            0 - TX CORE is possibly configured with initial settings.
**********************************************************************************************************/
uint32_t dptx_init(struct semaphore *pSem, uint8_t *pLink)
{
	uint32_t val;
	uint32_t retv = 0;
	uint32_t rev;

	/// \brief Validate DisplayPort TX Revision (DPTX Core)
	///        Supported Core ID:  Bit 31:16 - 0x000A
	///        Supported Core Rev: Bit 15:0  - 0x0403
	///        and
	///        Supported Core Rev: Bit 15:0  - 0x0404
	rev = asicregister_read32(VF111TX_CORE_ID);
	pr_debug("DPTX Core ID: 0x%x\n", rev);

	/// \note The DPTX Phy Reset must be set to a value prior to reading the Phy.
	///       If not set, the VAS will incur an access fault.
	///       Confirmed with ASIC team, this is correct behavior.
	///       Write a 0 for normal operation in the event the PHY config has been
	///       programmed.
	//asicregister_write32(VF111TX_PHY_RESET, 0x00);//register deprecated

	/// \brief Check if interrupts are enabled for the DisplayPort TX (DPTX Core)
	val = asicregister_read32(VF111TX_INTERRUPT_MASK);
	if (val != 0x0c) {//changed from 0x00 to 0x0c
		/// \note Core most likely has not been configured due to loss of aux power
		///       Enable all interrupts
		///       Bit 3:0 - 0x00
		asicregister_write32(VF111TX_INTERRUPT_MASK, 0x0c);//changed from 0x00 to 0x0c
		retv |= 0x01;
	}

	/// \breif Determine if core has been configured?
	///        Set clock divider (400 Mhz)
	val = asicregister_read32(VF111TX_AUX_CLOCK_DIVIDER);
	if (val != 0x190) {
		/// \note Core most likely has not been configured due to loss of aux power
		///       Setup clock divisor  400 MHz
		asicregister_write32(VF111TX_AUX_CLOCK_DIVIDER, 0x190);
		retv |= 0x02;
	}

	//added as per writeup doc
	/// \brief Enable transmitter before training. [R080h]
	val = asicregister_read32(VF111TX_TX_OUTPUT_EN);
	if (val != 0x01) {
		asicregister_write32(VF111TX_TX_OUTPUT_EN, 0x01);
		retv |= 0x10;
	}

	/// \breif Determine if core has been configured?
	///        Set the Enhanced Frame Enable
	val = asicregister_read32(VF111TX_ENHANCED_FRAME_EN);
	if (val != 0x01) {
		///       Set the Enhanced Frame Enable
		asicregister_write32(VF111TX_ENHANCED_FRAME_EN, 0x01);
		retv |= 0x08;
	}

	val = asicregister_read32(VF111TX_MAIN_STREAM_MISC0);
	if (val != 0x01) {
		///       Set the Main Stream Misc0
		asicregister_write32(VF111TX_MAIN_STREAM_MISC0, 0x01);
		retv |= 0x20;
	}

	/// \breif Determine if core has been configured?
	///        BW is last thing set after the PHY is initialized.
	///        BW is the first thing cleared before PHY is initialized.
	*pLink = asicregister_read32(VF111TX_LINK_BW_SET);//check
	if (*pLink == 0x00) {
		retv |= 0x04;
	}

	/// Initialize the AUX channel communication interface
	dptxaux_init(pSem);

	/// \brief Initialize the vswing and pre-emphasis settings based on
	///        platform attributes.
	///        If no values are present, predefined values will be used.
	//if (!dpwa_init())
	//{
		//dpwa_vswing(sizeof(g_swing_level),g_swing_level,g_swing_level_lbr);
		//dpwa_preemph(sizeof(g_preemphasis),g_preemphasis,g_preemphasis_lbr);
	//}

	/// \brief Initialize the voltage swing and preemphasis
	///        array sizes.  These sizes will stay constant for both
	///        lbr and hbr.
	//    g_serdes_cfg.vsw_pre_sz = sizeof(g_level_lbr);
	//    g_serdes_cfg.pre_sz = sizeof(g_preemphasis);

	return retv;
}

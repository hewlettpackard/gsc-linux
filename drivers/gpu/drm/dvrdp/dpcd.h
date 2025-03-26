// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is the header file to support the DisplayPort Configuration Data.
 *
 */

#ifndef DPCD_H
#define  DPCD_H

#include <linux/types.h>

/// Sink Device Addresses
/// Receiver Cabability Fields (000h - 00Bh)
#define DPCD_REV        (0x0000)
#define DPCD_MAX_LINK_RATE   (0x0001)
#define DPCD_MAX_LANE_CNT    (0x0002)
#define DPCD_MAX_DOWNSPREAD  (0x0003)
#define DPCD_NORP            (0x0004) // Number of receiver ports
#define DPCD_DWNSTREAMPORT_PRES     (0x0005)
#define DPCD_MAIN_LINK_CH_CODING    (0x0006)
#define DPCD_DWNSTREAMPORT_PORT_CNT (0x0007)
#define DPCD_RX_PORT0_CAP_0         (0x0008)
#define DPCD_RX_PORT0_CAP_1         (0x0009)
#define DPCD_RX_PORT1_CAP_0         (0x000a)
#define DPCD_RX_PORT1_CAP_1         (0x000b)

/// Link Configuration Fields (100h - 1FFh)
#define DPCD_LINK_BW_SET             (0x0100)
#define DPCD_LANE_CNT_SET            (0x0101)
#define DPCD_TRAINING_PAT_SET        (0x0102)
#define DPCD_TRAINING_LANE0_SET      (0x0103)
#define DPCD_TRAINING_LANE1_SET      (0x0104)
#define DPCD_TRAINING_LANE2_SET      (0x0105)
#define DPCD_TRAINING_LANE3_SET      (0x0106)
#define DPCD_DOWNSPREAD_CTRL         (0x0107)
#define DPCD_MAIN_LINK_CH_CODING_SET (0x0108)

/// Link/Sink Status Field (200h - 2017h)
#define DPCD_SINK_CNT                (0x0200)
#define DPCD_DEVICE_SERV_IRQ_VECTOR  (0x0201)
#define DPCD_LANE0_1_STATUS          (0x0202)
#define DPCD_LANE2_3_STATUS          (0x0203)
#define DPCD_LANE_ALIGN_STATUS_UPD   (0x0204)
#define DPCD_SINK_STATUS             (0x0205)
#define DPCD_ADJ_REQ_LANE0_1         (0x0206)
#define DPCD_ADJ_REQ_LANE2_3         (0x0207)
#define DPCD_TRAINING_SCORE_LANE0    (0x0208)
#define DPCD_TRAINING_SCORE_LANE1    (0x0209)
#define DPCD_TRAINING_SCORE_LANE2    (0x020A)
#define DPCD_TRAINING_SCORE_LANE3    (0x020B)
// Reserved 20Ch - 20Fh
#define DPCD_SYBL_ERR_CNT_LANE0      (0x0210)
#define DPCD_SYBL_ERR_CNT_LANE1      (0x0212)
#define DPCD_SYBL_ERR_CNT_LANE2      (0x0214)
#define DPCD_SYBL_ERR_CNT_LANE3      (0x0216)

/// Source Device Specific Field
#define DPCD_SRC_IEEE_OUI_0          (0x0300)
#define DPCD_SRC_IEEE_OUI_1          (0x0301)
#define DPCD_SRC_IEEE_OUI_2          (0x0302)
// Reserved for SRC Device specific usage 303h - 3FFh

/// Sink Device Specific Field
#define DPCD_SINK_IEEE_OUI_0         (0x0400)
#define DPCD_SINK_IEEE_OUI_1         (0x0401)
#define DPCD_SINK_IEEE_OUI_2         (0x0402)
// Reserved for Sink Device specific usage 403h - 4FFh

/// Sink Control Field
#define DPCD_SET_POWER               (0x0600)
// Reserved 601h - 6FFh

#define DPCD_LINK_RATE_5_4     (0x14)   // 5.4Gbps per lane
#define DPCD_LINK_RATE_2_7     (0x0a)   // 2.7Gbps per lane
#define DPCD_LINK_RATE_1_62    (0x06)   // 1.62Gbps per lane

#define DPCD_VER_1_1      (0x11)
#define DPCD_VER_1_2      (0x12)

/// \brief Macros for Device Service IRQ Vector
///        Definition can be found in DPCD register 201h
#define DPCD_IRQ_REMOTE_CONTROL   (0x01 << 0)
#define DPCD_IRQ_AUTOMATED_REQ    (0x01 << 1)
#define DPCD_IRQ_CP               (0x01 << 2)
#define DPCD_IRQ_SINK_SPECIFIC    (0x01 << 6)

#pragma pack(1)
/// \brief DPCD struct supporting DisplayPort 1.1a
/// \note The DPCD version is independant of the DisplayPort
///       version.
typedef struct _dpcd_rx_capabilty {
	uint8_t dpcd_rev;
	uint8_t max_link_rate;
	uint8_t max_lane_cnt;
	uint8_t max_dwnspread;
	uint8_t norp;
	uint8_t dwnstreamport_pres;
	uint8_t main_link_ch_coding;
	uint8_t dwnstreamport_cnt;
	uint8_t rx_port0_cap_0;
	uint8_t rx_port0_cap_1;
	uint8_t rx_port1_cap_0;
	uint8_t rx_port1_cap_1;
} dpcd_rx_capabilty_t;

/// \brief DPCD Link/ Sink Status Field
typedef struct _dpcd_ls_status {
	uint8_t sink_cnt;
	uint8_t dev_service_irq_vect;
	uint8_t lane0_1_status;
	uint8_t lane2_3_status;
	uint8_t lane_align_status_upd;
	uint8_t sink_status;
	uint8_t adj_req_lane0_1;
	uint8_t adj_req_lane2_3;
	uint8_t training_score_lane0;
	uint8_t training_score_lane1;
	uint8_t training_score_lane2;
	uint8_t training_score_lane3;
	uint16_t sybl_err_cnt_lane0;
	uint16_t sybl_err_cnt_lane1;
	uint16_t sybl_err_cnt_lane2;
	uint16_t sybl_err_cnt_lane3;
} dpcd_ls_status_t;

#pragma pack()

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * PHY wrapper control & status address definitions
 *
 */

    #define kWRAP_CDB_ENABLE 0x8018 // enable CDB access
    #define kWRAP_CDB_BUSSEL 0x801c // cdb bussel PHY input
    #define kWRAP_CDB_ACC_OK 0x8020 // read-only address for CDB register access_permitted PHY output signal
    #define kWRAP_CDB_REG_INIT_END 0x8024 // reg_init_end PHY input at bit 0

    #define kWRAP_PHY_REFCLK_ACTIVE 0x803c // read-only address
    #define kWRAP_PHY_CONFIG_RSTN 0x8040
    #define kWRAP_PHY_CMN_READY 0x8044 // read-only address
    #define kWRAP_PHY_LANE_EN_BITS 0x8048
    #define kWRAP_PHY_LANE_RSTN_BITS 0x804c

    #define kWRAP_PHY_PLL0_ENABLE 0x8050
    #define kWRAP_PHY_PLL0_STATUS 0x8054 // 3 bits defined below

    #define kWRAP_PHY_PLL1_ENABLE 0x8058
    #define kWRAP_PHY_PLL1_STATUS 0x805c // same 3 bits as PLL0_STATUS

    #define kWRAP_PHY_DP_LANE_MODE 0x8060
    #define kWRAP_PHY_DP_LANESET_READY 0x8064 // read-only address
    #define kWRAP_LANE_ELEC_IDLE_BITS 0x8070
    #define kWRAP_DP_DUAL_MODE_ENABLE 0x8074
    #define kWRAP_PHY_DP_LINK_RATE 0x8078
    #define kWRAP_DISABLE_AUTO_RXLINK 0x807c

    #define kWRAP_TX_DEEMPH_LANE0 0x8080
    #define kWRAP_TX_DEEMPH_LANE1 0x8084
    #define kWRAP_TX_DEEMPH_LANE2 0x8088
    #define kWRAP_TX_DEEMPH_LANE3 0x808c

    #define kWRAP_TX_VMARGIN_LANE0 0x8090
    #define kWRAP_TX_VMARGIN_LANE1 0x8094
    #define kWRAP_TX_VMARGIN_LANE2 0x8098
    #define kWRAP_TX_VMARGIN_LANE3 0x809c

    #define kWRAP_XCVR_PLLCLK0_ENABLE 0x80a0
    #define kWRAP_XCVR_PLLCLK1_ENABLE 0x80a4
    #define kWRAP_XCVR_PLLCLK2_ENABLE 0x80a8
    #define kWRAP_XCVR_PLLCLK3_ENABLE 0x80ac

    #define kWRAP_XCVR_PLLCLK0_EN_ACK 0x80b0 // read-only address
    #define kWRAP_XCVR_PLLCLK1_EN_ACK 0x80b4 // read-only address
    #define kWRAP_XCVR_PLLCLK2_EN_ACK 0x80b8 // read-only address
    #define kWRAP_XCVR_PLLCLK3_EN_ACK 0x80bc // read-only address

    #define kWRAP_XCVR_RATE_CHNG0_REQ 0x80c0
    #define kWRAP_XCVR_RATE_CHNG1_REQ 0x80c4
    #define kWRAP_XCVR_RATE_CHNG2_REQ 0x80c8
    #define kWRAP_XCVR_RATE_CHNG3_REQ 0x80cc

    #define kWRAP_XCVR_RATE_CHNG0_ACK 0x80d0 // read-only address
    #define kWRAP_XCVR_RATE_CHNG1_ACK 0x80d4 // read-only address
    #define kWRAP_XCVR_RATE_CHNG2_ACK 0x80d8 // read-only address
    #define kWRAP_XCVR_RATE_CHNG3_ACK 0x80dc // read-only address

    #define kWRAP_XCVR_MODE_LANE0 0x80e0
    #define kWRAP_XCVR_MODE_LANE1 0x80e4
    #define kWRAP_XCVR_MODE_LANE2 0x80e8
    #define kWRAP_XCVR_MODE_LANE3 0x80ec

    #define kWRAP_PHY_RX_DATA_ENABLE 0x80f0
    #define kWRAP_PHY_TX_DATA_ENABLE 0x80f4

    #define kWRAP_PHY_TEST_RDWR_ADDRESS 0x80f8 // test register for connection sanity check
    #define kWRAP_PHY_ID_REV 0x80fc // read-only address

    #define kWRAP_PHY_XCVR0_SCANIN0 0x8100
    #define kWRAP_PHY_XCVR0_SCANIN1 0x8104
    #define kWRAP_PHY_XCVR1_SCANIN0 0x8108
    #define kWRAP_PHY_XCVR1_SCANIN1 0x810c
    #define kWRAP_PHY_XCVR2_SCANIN0 0x8110
    #define kWRAP_PHY_XCVR2_SCANIN1 0x8114
    #define kWRAP_PHY_XCVR3_SCANIN0 0x8118
    #define kWRAP_PHY_XCVR3_SCANIN1 0x811c
    #define kWRAP_PHY_XCVR0_SCANOUT0 0x8120 // read-only address
    #define kWRAP_PHY_XCVR0_SCANOUT1 0x8124 // read-only address
    #define kWRAP_PHY_XCVR1_SCANOUT0 0x8128 // read-only address
    #define kWRAP_PHY_XCVR1_SCANOUT1 0x812c // read-only address
    #define kWRAP_PHY_XCVR2_SCANOUT0 0x8130 // read-only address
    #define kWRAP_PHY_XCVR2_SCANOUT1 0x8134 // read-only address
    #define kWRAP_PHY_XCVR3_SCANOUT0 0x8138 // read-only address
    #define kWRAP_PHY_XCVR3_SCANOUT1 0x813c // read-only address


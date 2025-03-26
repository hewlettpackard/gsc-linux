// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is the header file to support the DisplayPort debug commands.
 *
 */

#ifndef DPDBG_H
#define  DPDBG_H

#include <linux/types.h>

typedef enum {
	DP_DBG_DPCD = 0,
	DP_DBG_LINK_5_4,
	DP_DBG_LINK_2_7,
	DP_DBG_LINK_1_62,
	DP_DBG_VPD_STAT,
	DP_DBG_VPD_GET,
	DP_DBG_NULL
} ENUM_CMD_DPDBG;

typedef struct {
	char   label[64];
	uint32_t count;
} dpdbg_get_dat;

void dpdbg_cmd(const ENUM_CMD_DPDBG cmd);
int  dpdbg_get(int num, dpdbg_get_dat *dat);
extern int common_phy_link_speed_set(int link_rate);

extern int dp_init(void);
extern void hotplug_func(void);
extern void dptx_int_hdlr(void);
extern void clear_intr(void);

#endif

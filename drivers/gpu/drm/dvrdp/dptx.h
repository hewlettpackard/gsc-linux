// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This API for the DP TX Core.
 *
 */


//#include "SysUtils.h"
#ifndef DPTX_H
#define DPTX_H

//#include <semaphore.h>
#include <linux/types.h>

extern int phyfd;
extern int tps3_supported;
extern int link_training_success_flag;

uint32_t dptx_train(const uint8_t cLinkspeed, const uint8_t max_lane);
void dptx_cfg_phy(const uint32_t cLinkspeed);
void dptx_end(void);
uint32_t dptx_init(struct semaphore *pSem, uint8_t *pLink);
int common_phy_link_speed_set(int link_rate);

#endif

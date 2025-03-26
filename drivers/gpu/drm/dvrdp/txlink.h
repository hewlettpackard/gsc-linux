// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This API for the TX link policy.
 *
 */


#ifndef TXLINK_H
#define  TXLINK_H

#include <linux/types.h>
//#include <semaphore.h>

void txlink_init(struct semaphore *pSem, const uint8_t cState);
void txlink_start(const uint16_t cReg);
void txlink_service(const uint16_t cReg);
void txlink_end(const uint16_t cReg);

#endif

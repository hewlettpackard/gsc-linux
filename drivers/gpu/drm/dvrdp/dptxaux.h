// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This is the header file to support the AUX bus functionality.
 *
 */


//#include "SysUtils.h"
#ifndef DPTXAUX_H
#define  DPTXAUX_H

#include <linux/types.h>
//#include <semaphore.h>

#define DPTX_AUX_STATE_HPD           (0x01)
//#define DPTX_AUX_STATE_REQ_IN_PROG   (0x04)//(0x02)//modified check
//#define DPTX_AUX_STATE_REPLY_RECV    (0x01)//(0x04)//modified check
//#define DPTX_AUX_STATE_REPLY_TIMEOUT (0x08)//check

//added
#define DPTX_AUX_STATUS_REPLY_RECV    (0x01)
#define DPTX_AUX_STATUS_REQ_IN_PROG   (0x04)
#define DPTX_AUX_STATUS_REPLY_ERROR   (0x08)//check


#define DPTX_AUX_NATIVE_ACK     (0x0)
#define DPTX_AUX_I2C_ACK        (DPTX_AUX_NATIVE_ACK)
#define DPTX_AUX_NATIVE_NACK    (0x1)
#define DPTX_AUX_NATIVE_DEFER   (0x2)
#define DPTX_AUX_I2C_NACK       (0x4)
#define DPTX_AUX_I2C_DEFER      (0x8)

#define DPTX_AUX_ERR_STATE      (0x13)
#define DPTX_AUX_TIMEOUT        (0x12)
#define DPTX_AUX_READ_ERR       (0x11)
#define DPTX_AUX_TIMEOUT_SEM    (0x10)
#define DPTX_AUX_INVALID_ARG    (~0x0)

void dptxaux_init(struct semaphore *pSem);
uint32_t dptxaux_read(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal);
uint32_t dptxaux_write(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal);
uint32_t dptxaux_i2c_read(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal);

#endif

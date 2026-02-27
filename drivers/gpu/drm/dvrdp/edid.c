// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This module contains logic to read the EDID from an attached sink device.
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
#include "edid.h"
#include "dptxaux.h"

#define EDID_OFFSET      (0x50)
#define EDID_STRUCT_SZ   (sizeof(edid_t))

/**********************************************************************************
 *  Description:
 *     Common function to calculate/validate EDID checksum.
 *
 *  Note:
 *
 *
 **********************************************************************************/
static inline uint8_t checksum(uint8_t *pEdid, const uint32_t cSz)
{
	int i;
	uint8_t chksum = 0;

	for (i = 0; i < cSz; i++, pEdid++)
		chksum += *pEdid;

	return chksum;
}

/**********************************************************************************
 *  Description:
 *     Common function to read the EDID of the
 *     sink device designated by the input enum.
 *
 *  Note:
 *     The function will attempt a max of 'retry' reads
 *     upon a failed transaction.
 *
 *********************************************************************************/
static uint32_t edid_read(uint8_t *pEdid, const uint32_t cSz)
{
	uint32_t retv;
	uint32_t retry = 2;
	uint8_t  *pTop;

	pTop = pEdid;

	while (retry) {
		retv = dptxaux_i2c_read(EDID_OFFSET, cSz, pEdid);
		pr_debug("EDID Read DP (%d)\n", retv);

		if (retv == DPTX_AUX_I2C_ACK) {
			// Transaction complete
			// Reset pointer to top of array
			pEdid = pTop;
			// Verify checksum
			if (checksum(pEdid, sizeof(edid_t)) != 0)
				retv = EDID_INVALID_CHKSUM;

			break;
		}
		retry = retry>>1;
	}

	return (retv);
}

/*****************************************************************************************
 *  Description:
 *     API function to write the virtual EDID.
 *
 *  Note:
 *     Compiler issues a warning when pointer is used after validating it is not null.
 *     Hence the 'else' statement.
 ****************************************************************************************/
uint32_t edid_write_virt(uint8_t *pEdid, const uint32_t cSz)
{
	uint32_t retv;
	uint32_t value;

	if ((pEdid == NULL) || (cSz != EDID_STRUCT_SZ)) {
		retv = EDID_INVALID_ARG;
	} else {
		int i = 0;

		for (i = 0; i < cSz; i += 4, pEdid += 4) {
			value = *((uint32_t *)pEdid);
			asicregister_write32(EDIDRAMDATA_MGAEDID + i, value);
		}

		retv = EDID_SUCCESS;
	}
	return (retv);
}

/****************************************************************************************
 *  Description:
 *     API function to read the virtual EDID.
 *
 *  Note:
 *     Compiler issues a warning when pointer is used after validating it is not null.
 *     Hence the 'else' statement.
 ****************************************************************************************/
uint32_t edid_read_virt(uint8_t *pEdid, const uint32_t cSz)
{
	uint32_t retv;
	uint32_t value;

	if ((pEdid == NULL) || (cSz != EDID_STRUCT_SZ)) {
		retv = EDID_INVALID_ARG;
	} else {
		int i = 0;

		for (i = 0; i < cSz; i += 4, pEdid += 4) {
			value = asicregister_read32(EDIDRAMDATA_MGAEDID + i);
			*((uint32_t *)pEdid) = value;
		}

		retv = EDID_SUCCESS;
	}
	return (retv);
}

/****************************************************************************************
 *  Description:
 *     API function to read the DisplayPort (DP) EDID.
 *
 *  Note:
 *
 ***************************************************************************************/
uint32_t edid_read_dp(uint8_t *pEdid, const uint32_t cSz)
{
	uint32_t retv = EDID_INVALID_ARG;

	pr_debug("%s:%d fn:%s edid_size=%d\n", __FILE__, __LINE__, __func__, cSz);

	if ((pEdid != NULL) && (cSz == EDID_STRUCT_SZ))
		retv = edid_read(pEdid, cSz);

	pr_debug("%s:%d fn:%s retv=0x%x\n", __FILE__, __LINE__, __func__, retv);
	return retv;
}

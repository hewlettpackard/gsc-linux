// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This module contains the main logic for the DP transmitter.
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
#include "dptxaux.h"
#include "vf111tx.h"
//#include "SysUtils.h"

#define AUX_REPLY_TIME_FRACTION  (0x00068DB8 * 7)   // 700 microseconds

static struct semaphore *gReply;
/************************************************************************************************
 *  Description:
 *     Common function to conduct aux channel communication.
 *
 *  Note:
 *
 ************************************************************************************************/
static uint32_t dptxaux_common(const uint32_t cCmd)
{
	uint32_t retv = 0;
//    Time            twait = {0, AUX_REPLY_TIME_FRACTION};
//    SignedValue     sv_reply = 0;
    //struct timespec abs_timeout;
	volatile uint8_t  reg;
	uint8_t           timeout = 0;
	uint8_t           timeout_prog = 0;

	do {
		/// Write AUX command
		asicregister_write32(VF111TX_AUX_CMD, cCmd);
		/// \note An interrupt should be received for response and set a semaphore.  Use a timed wait
		///       in case interrupt is not received.
		//abs_timeout.tv_sec = 0;
		//abs_timeout.tv_nsec = 10000;//changed from 700 to 10000;
		usleep_range(10000, 12000);//check
		//if (sem_timedwait(gReply, &abs_timeout) == 0)
		if (down_trylock(gReply) == 0) {
			//reg = (0x0000000e & asicregister_read32(VF111TX_AUX_STATE));
			reg = (0x0000000f & asicregister_read32(VF111TX_AUX_STATUS));

			//fprintf(stderr,"DEBUGINFO: Read status 0x%x\n",reg);

			while ((reg & DPTX_AUX_STATUS_REQ_IN_PROG)) { //macro needs to change from 2 to 4, check
				/// \note Wait a small amount of time for reply to be queued.
				///       Reply was received due to semaphore being incremented.
				//usleep(5*100);
				udelay(5*100);
				//reg = (0x0000000e & asicregister_read32(VF111TX_AUX_STATE));
				reg = (0x0000000f & asicregister_read32(VF111TX_AUX_STATUS));
				if (timeout_prog > 4) {
					pr_err("FATAL: Req in progress for 2 ms: AUX_STATE=0x%02x\n", reg);
					break;
				}
				timeout_prog++;
			}
			if (reg & DPTX_AUX_STATUS_REPLY_RECV) {
				// Check response; only review last 4 bits
				retv = 0x0000000f & asicregister_read32(VF111TX_AUX_REPLY_CODE);
				/// \note: There appears to be a problem with the read timing.
				///        If the FIFO is read to quickly, you will not get to data
				///        which was posted.
				///        Taking a guess at the delay based on testing with serial port

				switch (retv) {
				case DPTX_AUX_NATIVE_ACK:
					//fprintf(stderr,"INFO:ACK\n");
				break;
				case DPTX_AUX_NATIVE_NACK:
					//fprintf(stderr,"WARNING:NACK\n");
				break;
				case DPTX_AUX_NATIVE_DEFER:
					//usleep(1*100);
					udelay(1*100);
					//fprintf(stderr,"INFO: defer\n");
				break;
				case DPTX_AUX_I2C_NACK:
					//fprintf(stderr,"WARNING:i2c NACK\n");
				break;
				case DPTX_AUX_I2C_DEFER:
					//usleep(1*100);
					udelay(1*100);
					//fprintf(stderr,"INFO:defer\n");
				break;
				default:
					pr_err("FATAL: %s %d Unknown response (0x%x)\n", __func__, __LINE__, retv);
				break;
				}
			} else if (reg & DPTX_AUX_STATUS_REPLY_ERROR) {
				pr_err("WARNING: %s %d Read timeout: AUX_STATE=0x%x\n", __func__, __LINE__, reg);
				if (timeout > 4) {
					retv = DPTX_AUX_TIMEOUT;
					pr_debug("%s %d retv=0x%x\n", __func__, __LINE__, retv);//added, remove later
					break;
				}
				timeout++;
			} else {
				reg = (0x0000000f & asicregister_read32(VF111TX_AUX_STATUS));
				retv = DPTX_AUX_ERR_STATE;
				pr_err("FATAL: %s %d Unknown AUX_STATE: AUX_STATUS=0x%02x retv=0x%x\n", __func__, __LINE__, reg, retv);
				break;
			}
		} else {
			if (timeout > 4) {
				reg = (0x0000000e & asicregister_read32(VF111TX_AUX_STATUS));
				retv = DPTX_AUX_TIMEOUT_SEM;
				pr_err("FATAL: %s %d Missed response interrupt. retv=0x%x\n", __func__, __LINE__, retv);
				break;
			}
				timeout++;
		}
	} while ((retv == DPTX_AUX_I2C_DEFER) || (retv == DPTX_AUX_NATIVE_DEFER));

	return retv;
}

/************************************************************************************************
 *  Description:
 *     Function to read from the sink device over the AUX bus.
 *
 *  Note:
 *
 ************************************************************************************************/
uint32_t dptxaux_read(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal)
{
	uint32_t retv;
	int j;

	pr_debug("%s %d Offset=0x%x size=%d\n", __func__, __LINE__, cOffset, cSz);
	/// \brief Validate parameters.  No more than 16 bytes can
	///        be read at one time.  Count is 0 based.
	if (cSz > 0x10 || pVal == NULL || cSz == 0) {
		retv = DPTX_AUX_INVALID_ARG;
	} else {
		/// \todo clean up
		for (j = 0; j < 5; j++) {
			/// Write AUX address
			asicregister_write32(VF111TX_AUX_ADDR, cOffset);
			// Size is 0 based; subtract 1
			retv = dptxaux_common((0x00900 | (cSz - 1)));
			if (retv == DPTX_AUX_NATIVE_ACK) {
				// Got a valid response
				// Check number of bytes received
				if (cSz == (0x0000001f & asicregister_read32(VF111TX_REPLY_DATA_COUNT))) {
					// Copy data to buffer
					int i;

					for (i = 0; i < cSz; i++) {
						*pVal = 0x000000ff & asicregister_read32(VF111TX_AUX_REPLY_DATA);
						pVal++;
					}
					break;
				}
			} else {
				pr_err("WARNING: %s: AUX Read Err Offset=0x%x ret=0x%x\n", __func__, cOffset, retv);
			}
		}
	}
	return (retv);
}

/************************************************************************************************
 *  Description:
 *     Function to write to the sink device over the AUX bus.
 *
 *  Note:
 *
 ************************************************************************************************/
uint32_t dptxaux_write(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal)
{
	uint32_t retv;
	uint8_t *temp;
	int    i, j;

	pr_debug("%s %d Offset=0x%x size=%d\n", __func__, __LINE__, cOffset, cSz);

	/// \brief Validate parameters.  No more than 16 bytes can
	///        be written at one time.  Count is 0 based.
	if (cSz > 0x10 || pVal == NULL || cSz == 0) {
		retv = DPTX_AUX_INVALID_ARG;
	} else {
		for (j = 0; j < 5; j++) {
			/// Write AUX address
			asicregister_write32(VF111TX_AUX_ADDR, cOffset);
			/// Initialize temp pointer to input; incase more than one write is
			/// attempted.
			temp = pVal;
			/// \todo Find out if the FIFO must be rewritten when write fails.
			/// Write byte(s) to FIFO
			for (i = 0; i < cSz; i++, temp++)
				asicregister_write32(VF111TX_AUX_WRITE_FIFO, (0xff & *temp));

			/// Size is 0 based; subtract 1
			//retv = asicregister_write32(cOffset, (0x00800 | (cSz - 1)));
			retv = dptxaux_common((0x00800 | (cSz - 1)));

			if (retv == DPTX_AUX_NATIVE_ACK)
				break;

			pr_err("WARNING: %s: AUX Write Err Offset=0x%x ret=0x%x\n", __func__, cOffset, retv);

		}
	}

	return (retv);
}

/************************************************************************************************
 *  Description:
 *     Function to read from the sink device over the aux i2c bus.
 *
 *  Note:
 *     Main use is to read the EDID.  This code should be refactored in the future to support other
 *     aux i2c reads.
 *
 ************************************************************************************************/
uint32_t dptxaux_i2c_read(const uint32_t cOffset, const uint8_t cSz, uint8_t *pVal)
{
	uint32_t          retv;
	volatile uint32_t reg;

	/// \brief Validate parameters.  No more than 16 bytes can
	///        be read at one time.  Count is 0 based.
	if (pVal == NULL || cSz == 0) {
		retv = DPTX_AUX_INVALID_ARG;
	} else {
		/// Check state of bus
		reg = (0x0000000f & asicregister_read32(VF111TX_AUX_STATUS));
		while (reg & 0x06) {
			pr_debug("%s INFO:Bus busy; status 0x%x\n", __func__, reg);
			//usleep(2*100*100);
			usleep_range(2*100*100, 3*100*100);
			reg = (0x0000000f & asicregister_read32(VF111TX_AUX_STATUS));
			// workaround
			/// Last read to complete transaction
			retv = dptxaux_common(0x010f);
			pr_debug("%s Complete the flying request retv=0x%x\n", __func__, retv);
			break;
		}

		/// Write AUX address to read
		asicregister_write32(VF111TX_AUX_ADDR, cOffset);
		/// Write sub address to FIFO; starting offset to read.
		asicregister_write32(VF111TX_AUX_WRITE_FIFO, 0);

		/// Execute i2c aux command
		retv = dptxaux_common(0x0400);
		pr_debug("%s dptxaux_common-1 retv=0x%x", __func__, retv);
		if ((retv == DPTX_AUX_I2C_ACK) || (retv == DPTX_AUX_NATIVE_ACK)) {
			int j;

			for (j = 0; j < ((cSz/0x10)-0x01); j++) {
				/// Execute i2c aux command to read 16 bytes
				retv = dptxaux_common(0x050f);
				pr_debug("%s %d dptxaux_common-2 retv=0x%x\n", __func__, __LINE__, retv);
				if (retv == DPTX_AUX_I2C_ACK) {
					// Got a valid response
					// Check number of bytes received
					int cnt_temp = 0;//TODO: .added. check later

					cnt_temp = (0x0000001f & asicregister_read32(VF111TX_REPLY_DATA_COUNT));
					//if(0x10 == (0x0000001f & asicregister_read32(VF111TX_REPLY_DATA_COUNT)))
					if (1) { //commented (0x10 == cnt_temp)
						// Copy data to buffer
						int i;

						for (i = 0; i < 0x10; i++) {
							*pVal = 0x000000ff & asicregister_read32(VF111TX_AUX_REPLY_DATA);
							pVal++;
						}
					} else {
						retv = DPTX_AUX_READ_ERR;
						pr_err("%s %d j=%d cnt_temp=%d else read error retv=0x%x\n", __func__, __LINE__, j, cnt_temp, retv);//added, remove later
						break;
					}
				} else {
					break;
				}
			}
			if (retv == DPTX_AUX_I2C_ACK) {
				/// Last read to complete transaction
				retv = dptxaux_common(0x010f);
				pr_debug("%s %d dptxaux_common-3 retv=0x%x\n", __func__, __LINE__, retv);
				if (retv == DPTX_AUX_I2C_ACK) {
					// Got a valid response
					// Check number of bytes received
					if (1) { //commented check (0x10 == (0x0000001f & asicregister_read32(VF111TX_REPLY_DATA_COUNT)))
						// Copy data to buffer
						int i;

						for (i = 0; i < 0x10; i++) {
							*pVal = 0x000000ff & asicregister_read32(VF111TX_AUX_REPLY_DATA);
							pVal++;
						}
					} else {
						retv = DPTX_AUX_READ_ERR;
						pr_err("%s %d else read error-2 retv=0x%x\n", __func__, __LINE__, retv);//added, check and remove later
					}
				}
			}
		}
	}
	return (retv);
}


/************************************************************************************************
 *  Description:
 *     Function to init the AUX bus interface.
 *
 *  Note:
 *     Semaphore is needed to determine when the a request or a response has completed.
 *
 ************************************************************************************************/
void dptxaux_init(struct semaphore *pSem)
{
	/// Initialize the semaphore used for response interrupt
	gReply = pSem;
}

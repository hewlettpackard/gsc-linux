// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This API to read the EDID.  Reading the EDID is part of the stream policy.
 *
 */


#ifndef EDID_H
#define  EDID_H

#include <linux/types.h>

#define EDID_SUCCESS       (0x00)
#define EDID_INVALID_CHKSUM (0xfc)
#define EDID_VASCOM_ERR    (0xfd)
#define EDID_INIT_IN_PROG  (0xfe)
#define EDID_INVALID_ARG   (0xff)

/// \brief Data struct to define EDID
///        Struct is based on EDID revision 1.4
#pragma pack (1)
typedef struct _edid {
	uint8_t  hdr[8];
	uint16_t id_manf_name;
	uint16_t id_prod_code;
	uint32_t id_ser_num;  // Optional
	uint8_t  wk_manf;     // Optional
	uint8_t  yr_manf;
	uint8_t  ver_num;
	uint8_t  rev_num;
	uint8_t  video_input;
	uint8_t  max_horiz_img_sz;
	uint8_t  max_vert_img_sz;
	uint8_t  disp_transfer_char;
	uint8_t  feature_support;
	uint8_t  red_grn_low_bits;
	uint8_t  blue_white_low_bits;
	uint8_t  red_x;
	uint8_t  red_y;
	uint8_t  grn_x;
	uint8_t  grn_y;
	uint8_t  blue_x;
	uint8_t  blue_y;
	uint8_t  white_x;
	uint8_t  white_y;
	uint8_t  estab_timings_1;    // Optional
	uint8_t  estab_timings_2;    // Optional
	uint8_t  manf_resvd_timings;
	uint16_t std_timing_id_1;    // Optional
	uint16_t std_timing_id_2;    // Optional
	uint16_t std_timing_id_3;    // Optional
	uint16_t std_timing_id_4;    // Optional
	uint16_t std_timing_id_5;    // Optional
	uint16_t std_timing_id_6;    // Optional
	uint16_t std_timing_id_7;    // Optional
	uint16_t std_timing_id_8;    // Optional
	uint8_t  pref_timing_mode[18];        // Prefered Timing Mode; Required
	uint8_t  detailed_timing_desc_2[18];  // Optional for 1.4, but recommended
	uint8_t  detailed_timing_desc_3[18];  // Optional for 1.4, but recommended
	uint8_t  detailed_timing_desc_4[18];  // Optional for 1.4, but recommended
	uint8_t  ext_flag;  // Optional (Other descriptor blocks)
	uint8_t  chksum;
} edid_t;
#pragma pack ()

uint32_t edid_write_virt(uint8_t *pEdid, const uint32_t cSz);
uint32_t edid_read_virt(uint8_t *pEdid, const uint32_t cSz);
uint32_t edid_read_dp(uint8_t *pEdid, const uint32_t cSz);
uint32_t edid_read_vga(uint8_t *pEdid, const uint32_t cSz);

/* dp_vid_mod_reg16()
 * API to read/modify/write a register
 */
uint32_t dp_vid_mod_reg16(uint32_t reg, uint32_t setmask, uint32_t clearmask, uint16_t *val);

/* dp_vid_mode_config()
 * Signal a video mode configuration change
 */
uint32_t dp_vid_mode_config(uint32_t val);

void debug_dpcd(void);
#endif

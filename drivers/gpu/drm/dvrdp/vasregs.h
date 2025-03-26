// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This is the header file for Virtual Addressing of the DP IO registers
 *
 */


/* Module definitions */
#ifndef VASREGS_H
#define  VASREGS_H

#include <linux/types.h>

//
// Virtual address space registers (These must match the
// system register mappings in drvdp)
//
extern char *r_dptx_base;
extern char *r_dprx_base;
extern char *r_gd_base;
extern char *r_v_edid_base;
//extern volatile uint8_t *r_dptx_base;
//extern volatile uint8_t *r_dprx_base;
//extern volatile uint8_t *r_gd_base;
//extern volatile uint8_t *r_v_edid_base;

#define R_DPTX_BASE    ((unsigned long)r_dptx_base)
#define R_DPRX_BASE    ((unsigned long)r_dprx_base)
#define R_IOP_BASE     ((unsigned long)r_gd_base - 0x1c0)
#define R_CFG1_BASE    ((unsigned long)r_v_edid_base)

//FNC1
#define  SVGAPCIVDID                      (R_CFG1_BASE + 0x000) // SVGA PCI Vendor/Device ID
#define  SVGAPCICMD                       (R_CFG1_BASE + 0x004) // SVGA PCI Command
#define  SVGAPCISTAT                      (R_CFG1_BASE + 0x006) // SVGA PCI Status
#define  SVGARID                          (R_CFG1_BASE + 0x008) // SVGA Revision ID
#define  SVGACC                           (R_CFG1_BASE + 0x009) // SVGA Class Code
#define  SVGAPCICLS                       (R_CFG1_BASE + 0x00c) // SVGA PCI Cache Line Size
#define  SVGAPCILT                        (R_CFG1_BASE + 0x00d) // SVGA PCI Latency Timer
#define  SVGAPCIH                         (R_CFG1_BASE + 0x00e) // SVGA PCI Header type
#define  SVGAFBBADDR                      (R_CFG1_BASE + 0x010) // SVGA Frame Buffer Base Address
#define  SVGAMBADDR                       (R_CFG1_BASE + 0x014) // SVGA
#define  SVGAPDMAABADDR                   (R_CFG1_BASE + 0x018) // SVGA Pseudo-DMA Aperture Base Address
#define  SVGASVDID                        (R_CFG1_BASE + 0x02c) // SVGA Subsystem Vendor/Device ID
#define  SVGAEROMBADDR                    (R_CFG1_BASE + 0x030) // SVGA Expansion ROM Base Address
#define  SVGAPCIINTL                      (R_CFG1_BASE + 0x03c) // SVGA PCI Interrupt Line
#define  SVGAPCIINTP                      (R_CFG1_BASE + 0x03d) // SVGA PCI Interrupt Pin
#define  SVGAPCIMG                        (R_CFG1_BASE + 0x03e) // SVGA PCI Minimum Grant
#define  SVGAPCIML                        (R_CFG1_BASE + 0x03f) // SVGA PCI Maximum Latency
#define  DMCTL_DECR                       (R_CFG1_BASE + 0x080) // Display Management Control
  #define MON2PRSNOVREN 0x00080000
  #define MON2PRSNOVR   0x00040000
  #define MON1PRSNOVREN 0x00020000
  #define MON1PRSNOVR   0x00010000
  #define EXTDISPDIS    0x00001000
  #define G200_MISCI    0x00000700
  #define GEN_HPEVENT   0x00000040
  #define DRPWRDN       0x00000020
  #define MON_SEL       0x00000010
  #define AUTODACDN     0x00000002
  #define AUTOMGT_EN    0x00000001

#define  DESTAT_DESR                      (R_CFG1_BASE + 0x084) // Display Event Status
#define  DEINTM_DEIMR                     (R_CFG1_BASE + 0x088) // Display Event Interrupt Mask
#define  VEDIDCTL_VEDIDEN                 (R_CFG1_BASE + 0x08e) // Virtual EDID Control (VEDIDEN)
#define  SVGAF_VGAFAULT                   (R_CFG1_BASE + 0x090) // SVGA Fault
#define  VRM_VIDRMSK                      (R_CFG1_BASE + 0x094) // Video Reset Mask (VIDRMSK)
#define  FN1ST_FN1SPAD                    (R_CFG1_BASE + 0x09c) // FN1 Scratchpad Test
#define  SVGAEROMCFG_VXROMCFG             (R_CFG1_BASE + 0x0a0) // SVGA Expansion ROM Configuration
#define  SVGAPCIPMCI                      (R_CFG1_BASE + 0x0a8) // SVGA PCI Power Management Capability Identifier
#define  SVGAPCIPMCNIP                    (R_CFG1_BASE + 0x0a9) // SVGA PCI Power Management Capability Next Item Pointer
#define  SVGAMSIPCICI                     (R_CFG1_BASE + 0x0b0) // SVGA MSI PCI Capability Identifier
#define  SVGAMSIPCICNIP                   (R_CFG1_BASE + 0x0b1) // SVGA MSI PCI Capability Next Item Pointer
#define  SVGAPCIECI                       (R_CFG1_BASE + 0x0c0) // SVGA PCI-E Capability Identifier
#define  SVGAPCIECNIP                     (R_CFG1_BASE + 0x0c1) // SVGA PCI-E Capability Next Item Pointer
#define  SVGAPCIELC                       (R_CFG1_BASE + 0x0cc) // SVGA PCI-E Link Capabilities
#define  SVGAPCIEDC2                      (R_CFG1_BASE + 0x0e4) // SVGA PCI-E Device Capabilities 2
#define  SVGAPCIEDCTL2                    (R_CFG1_BASE + 0x0e8) // SVGA PCI-E Device Control 2
#define  SVGAPCIEDSTAT2                   (R_CFG1_BASE + 0x0ea) // SVGA PCI-E Device Status 2
#define  SVGAPCIELC2                      (R_CFG1_BASE + 0x0ec) // SVGA PCI-E Link Capabilities 2
#define  SVGAPCIELCTL2                    (R_CFG1_BASE + 0x0f0) // SVGA PCI-E Link Control 2
#define  SVGAPCIELSTAT2                   (R_CFG1_BASE + 0x0f2) // SVGA PCI-E Link Status 2
#define  SVGAPCIEECH                      (R_CFG1_BASE + 0x100) // SVGA PCI-E Enhanced Capabilities Header
#define  SVGAPCIEUESTAT                   (R_CFG1_BASE + 0x104) // SVGA PCI-E Uncorrectable Error Status
#define  SVGAPCIEUEM                      (R_CFG1_BASE + 0x108) // SVGA PCI-E Uncorrectable Error Mask
#define  SVGAPCIEUES                      (R_CFG1_BASE + 0x10c) // SVGA PCI-E Uncorrectable Error Severity
#define  SVGAPCIECESTAT                   (R_CFG1_BASE + 0x110) // SVGA PCI-E Correctable Error Status
#define  SVGAPCIECEM                      (R_CFG1_BASE + 0x114) // SVGA PCI-E Correctable Error Mask
#define  SVGAPCIEAERCCTL                  (R_CFG1_BASE + 0x118) // SVGA PCI-E AER Capabilities and Control
#define  SVGAPCIEAER_AERHL1               (R_CFG1_BASE + 0x11c) // SVGA PCI-E Advanced Error Reporting (AER) Header Log 1
#define  SVGAPCIEAER_AERHL2               (R_CFG1_BASE + 0x120) // SVGA PCI-E Advanced Error Reporting (AER) Header Log 2
#define  SVGAPCIEAER_AERHL3               (R_CFG1_BASE + 0x124) // SVGA PCI-E Advanced Error Reporting (AER) Header Log 3
#define  SVGAPCIEAER_AERHL4               (R_CFG1_BASE + 0x128) // SVGA PCI-E Advanced Error Reporting (AER) Header Log 4
#define  EDIDRAMDATA_MGAEDID              (R_CFG1_BASE + 0x300) // EDID RAM Data

#define  GD_MCFG_GDCFG                      (R_IOP_BASE + 0x1c0) // Global Display Management Configuration
  #define MEASURE_EN    0x0040
  #define DPTXAUTO_EN   0x0020
  #define DACDNOVRD     0x0010
  #define GD_AUTODACDN  0x0008
  #define EXTPRDETMODE  0x0004
  #define AUTOPRDET_EN  0x0002
  #define GLPMODE_EN    0x0001

#define  GD_MCTL_GDCTRL                     (R_IOP_BASE + 0x1c2) // Global Display Management Control
#define  GD_ESTAT_GDESR                     (R_IOP_BASE + 0x1c4) // Global Display Event Status
  #define SRCCHGDONE    0x80000000
  #define DPSNKRDY      0x04000000
  #define SRCSIGNALPRSN 0x02000000
  #define CSRM          0x00100000
  #define E_MONPRSN     0x00040000
  #define I_MONPRSN     0x00020000
  #define IVGATERM      0x00010000
  #define SRCCHGDONEEVT 0x00008000
  #define SRCDETECT     0x00000200
  #define SRCMODE       0x00000100
  #define DP_RX_NT      0x00000080
  #define DP_TX_INT     0x00000040
  #define DACOVERVOLT   0x00000020
  #define DACMONPRSN    0x00000001

#define  GD_EM_GDEIMR                       (R_IOP_BASE + 0x1c8) // Global Display Event Mask
#define  GD_HDM_GDHDISP                     (R_IOP_BASE + 0x1d0) // Global Display Horizontal Display Measurement (GDHDISP)
#define  GD_HSSM_GDHSSTRT                   (R_IOP_BASE + 0x1d4) // Global Display Horizontal Sync Start Measurement (GDHSSTRT)
#define  GD_HSWM_GDHSWDTH                   (R_IOP_BASE + 0x1d8) // Global Display Horizontal Sync Width Measurement (GDHSWDTH)
#define  GD_HTM_GDHTOT                      (R_IOP_BASE + 0x1dc) // Global Display Horizontal Total Measurement (GDHTOT)
#define  GD_VDM_GDVDISP                     (R_IOP_BASE + 0x1e0) // Global Display Vertical Display Measurement (GDVDISP)
#define  GD_VSSM_GDVSSTRT                   (R_IOP_BASE + 0x1e4) // Global Display Vertical Sync Start Measurement (GDVSSTRT)
#define  GD_VSWM_GDVSWDTH                   (R_IOP_BASE + 0x1e8) // Global Display Vertical Sync Width Measurement (GDVSWDTH)
#define  GD_VTM_GDVTOT                      (R_IOP_BASE + 0x1ec) // Global Display Vertical Total Measurement (GDVTOT)
#define  GD_MM_GDMISCM                      (R_IOP_BASE + 0x1f0) // Global Display Miscellaneous Measurement (GDMISCM)
#define  GD_PSMISC0O_GDPMISC0               (R_IOP_BASE + 0x1f4) // Global Display Port Source MISC0 Override (GDPMISC0)
#define  GD_PSMISC1O_GDPMISC1               (R_IOP_BASE + 0x1f8) // Global Display Port Source MISC1 Override (GDPMISC1)

#define  DP_PHYPIPECFGI                    (R_DPTX_BASE + 0xfc0) // DisplayPort PHY PIPE Configuration Index
#define  DP_PHYPIPECFGDATA                 (R_DPTX_BASE + 0xfc4) // DisplayPort PHY PIPE Configuration Data
#define  DP_INT_OFFSET                     (R_DPTX_BASE + 0x140) // TRILINEAR INT OFFSET
#define  DP_RXEDIDRAMDATA_DPRXEDID         (R_DPRX_BASE + 0xe00) // DPRX EDID RAM Data

#define asicregister_write8(address, value) iowrite8((uint8_t)value, (volatile void __iomem *)(address))
#define asicregister_write16(address, value) iowrite16((uint16_t)value, (volatile void __iomem *)(address))
#define asicregister_write32(address, value) iowrite32((uint32_t)value, (volatile void __iomem *)(address))

#define asicregister_read8(address) ioread8((volatile void __iomem *)(address))
#define asicregister_read16(address) ioread16((volatile void __iomem *)(address))
#define asicregister_read32(address) ioread32((volatile void __iomem *)(address))

#endif

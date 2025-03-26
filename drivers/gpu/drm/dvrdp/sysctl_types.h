// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */

#ifndef _INC_sysctl_types_
#define _INC_sysctl_types_
#ident "@(#)$Id: sysctl_types.h,v 1.1 2013/06/07 19:11:03 ssh Exp $"

/*
 * Type definitions, used just about everywhere
 */
typedef volatile unsigned char   vuint8;       /* 8-bit  device register  */
typedef volatile unsigned short  vuint16;      /* 16-bit device register  */
typedef volatile unsigned long   vuint32;      /* 32-bit device register  */
#ifndef CORE_TYPES_H
/* NOTE: When building within the AMI MegaRAC-SPX environment,
 * coreTypes.h will define these basic types */
typedef unsigned char            uint8;        /* unsigned 8-bit  integer */
typedef unsigned short           uint16;       /* unsigned 16-bit integer */
typedef unsigned int             uint32;       /* unsigned 32-bit integer */
typedef signed char              int8;         /* 8-bit  signed integer   */
typedef signed short             int16;        /* 16-bit signed integer   */
typedef signed int               int32;        /* 32-bit signed integer   */
#endif /* !CORE_TYPES_H */
typedef uint8                    boolean;      /* boolean value           */

#ifndef True
#define True 1
#endif

#ifndef False
#define False 0
#endif

/*
 * definitions for the system type (L1 or L2)
 *
 * also include any include files that are integral to the controller
 * (those that define hardware-specific features)
 */

#if defined(__coldfire)
#ifndef L1_CONTROLLER
#define L1_CONTROLLER
#endif
#include "mcf5206e.h"
#include "hardware.h"
#elif defined(NEWL1_CONTROLLER)
/* can't trigger off the HW architecture for the new L1, as some code
   for armlinux is L2/L3 level code and some is part of the L1 app */
#ifndef L1_CONTROLLER
#define L1_CONTROLLER
#endif
#include "hardware.h"
#elif defined(__ppc) || defined(__powerpc__) || defined(armlinux)
#define L2_CONTROLLER
#ifndef linux
/* only need these #include's for Nucleus */
#include "mpc860.h"
#include "Get_IMMR.h"
#endif
#else
/* must be an L3 controller--how do we check for this?? */
#define L3_CONTROLLER
#endif


/*
 * Byte ordering:  for GCC compilers, include the byte ordering includes,
 * for all others they can be hard-coded (to big-endian order).
 * Note that this also gets us the bitfield packing order macros.
 */

#ifdef __GNUC__
#   include <endian.h>
#  ifndef BOOTLOADER
#   include <netinet/in.h>
#  endif
#else
    /* all non-GCC architectures are big endian */
#   define __BIG_ENDIAN 0x1234
#   define __LITTLE_ENDIAN 0x4321
#   define __BYTE_ORDER __BIG_ENDIAN
#   define __BIG_ENDIAN_BITFIELD 1
#   ifndef htonl
#   define htonl(x) (x)
#   endif
#   ifndef ntohl
#   define ntohl(x) (x)
#   endif
#   ifndef htons
#   define htons(x) (x)
#   endif
#   ifndef ntohs
#   define ntohs(x) (x)
#   endif
#if 0
#   define htonl(x) ((uint32) x)
#   define ntohl(x) ((uint32) x)
#   define htons(x) ((uint16) x)
#   define ntohs(x) ((uint16) x)
#endif
#endif


#endif /* _INC_sysctl_types_ */

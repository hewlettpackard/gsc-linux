/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCPKM_NETLINK_H
#define ___ZTCPKM_NETLINK_H

#include <linux/types.h>

#define NETLINK_ZTCP 31

bool ztcpkm_netlink_init(void);
bool ztcpkm_netlink_fini(void);

bool ztcpkm_netlink_send(const char *msg);

#endif

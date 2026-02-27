/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCP_UTIL_H
#define ___ZTCP_UTIL_H

#include <linux/key.h>
#include <linux/types.h>

#define ZTCP_AUDIT 2699

#define strsep_foreach(token, buf, delim) \
	while ((token = strsep(buf, delim)) != NULL) \

#define ZTCPKM_AUDITD(severity, information)

void ztcpkm_log_info(char *fmt, ...);
void ztcpkm_log_warn(char *fmt, ...);
void ztcpkm_log_err(char *fmt, ...);
void ztcpkm_log_kernel(char *fmt, ...);

#define Z_KFREE_NULL(ptr) if (ptr) { kfree(ptr); ptr = NULL; }
#define Z_FREEPAGE_NULL(ptr) if (ptr) { free_page((unsigned long)ptr); ptr = NULL; }
#define Z_PTR_CHECK_RETURN_NONE(ptr, msg) \
        if (!ptr) { \
                ztcpkm_log_kernel("%s: null %s\n", msg, #ptr); \
                return; \
        }
#define Z_PTR_CHECK_RETURN_VALUE(ptr, rc, msg) \
        if (!ptr) { \
                ztcpkm_log_kernel("%s: null %s\n", msg, #ptr); \
                return rc; \
        }

#endif

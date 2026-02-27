// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <keys/asymmetric-type.h>
#include <linux/audit.h>
#include <linux/cred.h>
#include <linux/key.h>
#include <linux/slab.h>

#include "ztcputil.h"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MSG_BUF_SZ 1024

#define ZTCPKM_FMT(buf, size, fmt) \
	va_list args; \
	va_start(args, fmt); \
	vsnprintf(buf, size, fmt, args); \
	va_end(args);

inline void ztcpkm_log_info(char *fmt, ...)
{
	char auditd_msg[MSG_BUF_SZ];

	Z_PTR_CHECK_RETURN_NONE(fmt, __func__)
	{ // suppress "warning: ISO C90 forbids mixed declarations and code"
		ZTCPKM_FMT(auditd_msg, MSG_BUF_SZ, fmt);
	}
	pr_info("%s", auditd_msg);
}

inline void ztcpkm_log_warn(char *fmt, ...)
{
	char auditd_msg[MSG_BUF_SZ];

	Z_PTR_CHECK_RETURN_NONE(fmt, __func__)
	{ // suppress "warning: ISO C90 forbids mixed declarations and code"
		ZTCPKM_FMT(auditd_msg, MSG_BUF_SZ, fmt);
	}
	pr_warn("%s", auditd_msg);
}

inline void ztcpkm_log_err(char *fmt, ...)
{
	char auditd_msg[MSG_BUF_SZ];

	Z_PTR_CHECK_RETURN_NONE(fmt, __func__)
	{ // suppress "warning: ISO C90 forbids mixed declarations and code"
		ZTCPKM_FMT(auditd_msg, MSG_BUF_SZ, fmt);
	}
	pr_err("%s", auditd_msg);
}

inline void ztcpkm_log_kernel(char *fmt, ...)
{
	char auditd_msg[MSG_BUF_SZ];

	Z_PTR_CHECK_RETURN_NONE(fmt, __func__)
	{ // suppress "warning: ISO C90 forbids mixed declarations and code"
		ZTCPKM_FMT(auditd_msg, MSG_BUF_SZ, fmt);
	}
	pr_err("%s", auditd_msg);
}

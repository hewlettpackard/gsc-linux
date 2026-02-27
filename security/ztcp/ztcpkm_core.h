/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCPKM_CORE_H
#define ___ZTCPKM_CORE_H

#include <linux/binfmts.h>

/*
 * int security_bprm_check(struct linux_binprm *bprm);
 */
int ztcpkm_security_bprm_check(struct linux_binprm *bprm);

#endif

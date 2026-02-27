// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/init.h>
#ifdef MODULE
#include <linux/module.h>
#endif
#include <linux/lsm_hooks.h>

#include "ztcpkm_core.h"

static struct security_hook_list ztcpkm_lsm_hooks[] __ro_after_init = {
    LSM_HOOK_INIT(bprm_check_security, ztcpkm_security_bprm_check),
};

int ztcpkm_enabled __ro_after_init = 1;
const struct lsm_id ztcp_lsmid = {
	.name = "ztcp",
	.id = 114,
};

static int ztcpkm_lsm_init(void)
{
    // name ("ztcp") must match what is in the .config
    security_add_hooks(ztcpkm_lsm_hooks, ARRAY_SIZE(ztcpkm_lsm_hooks), &ztcp_lsmid);
    return 0;
}

DEFINE_LSM(ztcp) = {
    .name = "ztcp",
    .order = LSM_ORDER_MUTABLE,
    .flags = LSM_FLAG_LEGACY_MAJOR,
    .enabled = &ztcpkm_enabled,
    .init = ztcpkm_lsm_init,
};

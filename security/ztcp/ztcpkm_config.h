/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCPKM_CONFIG

#include <linux/key.h>
#include <linux/mutex.h>

struct ztcpkm_config
{
    uint32_t num_of_runc_path;
    char **runc_path;

    struct key *iLOContainerCert;
};

extern struct ztcpkm_config zconfig;
extern struct mutex zconfig_lock;

int ztcpkm_config_init(void);
int ztcpkm_config_fini(void);

#endif

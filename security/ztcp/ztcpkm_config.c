// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include "ztcpkm_config.h"
#include "ztcputil.h"

#include <linux/slab.h>

struct ztcpkm_config zconfig = {
	0, NULL, NULL
};

DEFINE_MUTEX(zconfig_lock);

static char predefined_runc_path[][PATH_MAX] = { "/bin/crun", "/usr/bin/crun" };

int ztcpkm_config_init(void)
{
    /* TODO: Read config from file (/etc/ztcp/config...) */
    int i;
    int ret;

    mutex_lock(&zconfig_lock);

    zconfig.runc_path = kmalloc((sizeof(predefined_runc_path) / sizeof(predefined_runc_path[0])) * sizeof(char *), GFP_KERNEL);
    if (zconfig.runc_path == NULL) {
        ztcpkm_log_kernel("Failed to allocate memory for runc_path array");
        mutex_unlock(&zconfig_lock);
        return -1;
    }

    for (i = 0; i < sizeof(predefined_runc_path) / sizeof(predefined_runc_path[0]); i++) {
        zconfig.runc_path[zconfig.num_of_runc_path] = kstrdup(predefined_runc_path[i], GFP_KERNEL);
        if (zconfig.runc_path[zconfig.num_of_runc_path] == NULL) {
            ztcpkm_log_kernel("Failed to allocate memory for runc pathname");
            while (zconfig.num_of_runc_path > 0) {
                zconfig.num_of_runc_path--;
                kfree(zconfig.runc_path[zconfig.num_of_runc_path]);
            }
            kfree(zconfig.runc_path);
            zconfig.runc_path = NULL;
            mutex_unlock(&zconfig_lock);
            return -1;
        }
        zconfig.num_of_runc_path++;
    }

    mutex_unlock(&zconfig_lock);
    return 0;
}

int ztcpkm_config_fini(void)
{
    int i;

    mutex_lock(&zconfig_lock);

    for (i = 0; i < zconfig.num_of_runc_path; i++) {
        kfree(zconfig.runc_path[i]);
        zconfig.runc_path[i] = NULL;
    }

    kfree(zconfig.runc_path);
    zconfig.runc_path = NULL;
    zconfig.num_of_runc_path = 0;

    mutex_unlock(&zconfig_lock);

    return 0;
}

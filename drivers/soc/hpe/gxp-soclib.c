// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>

struct class *soc_class;

static int __init gxp_soclib_init(void)
{
	soc_class = class_create("soc");
	if (IS_ERR(soc_class))
		return PTR_ERR(soc_class);
	return 0;
}

module_init(gxp_soclib_init);


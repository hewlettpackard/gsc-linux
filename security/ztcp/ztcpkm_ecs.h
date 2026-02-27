/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCPKM_ECS_H
#define ___ZTCPKM_ECS_H

#include <linux/completion.h>
#include <linux/types.h>

/*
 * ecs stands for execution control struct
 */
struct ztcpkm_ecs {
	struct list_head list;
	pid_t pid;
	struct completion ztcpsa_responded;
	atomic_t result;
};

bool add_ztcpkm_ecs(struct ztcpkm_ecs *);
bool del_ztcpkm_ecs(struct ztcpkm_ecs *);
struct ztcpkm_ecs *find_ztcpkm_ecs(pid_t);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/mutex.h>
#include "ztcpkm_ecs.h"

LIST_HEAD(ztcpkm_ecs_list);
DEFINE_MUTEX(ecs_list_lock);

bool add_ztcpkm_ecs(struct ztcpkm_ecs *ecs)
{
	if (!ecs)
		return false;

	mutex_lock(&ecs_list_lock);
	pr_debug("ecs of pid %d added\n", ecs->pid);
	list_add_tail(&ecs->list, &ztcpkm_ecs_list);
	mutex_unlock(&ecs_list_lock);

	return true;
}

bool del_ztcpkm_ecs(struct ztcpkm_ecs *ecs)
{
	struct list_head *pos, *next;
	struct ztcpkm_ecs *tmp = NULL;
	bool found = false;

	if (!ecs)
		return false;

	mutex_lock(&ecs_list_lock);
	
	/* Validate ecs is actually in the list before deleting */
	list_for_each_safe(pos, next, &ztcpkm_ecs_list) {
		tmp = list_entry(pos, struct ztcpkm_ecs, list);
		if (tmp == ecs) {
			found = true;
			break;
		}
	}

	if (found) {
		pr_debug("ecs of pid %d deleted\n", ecs->pid);
		list_del(&ecs->list);
	}

	mutex_unlock(&ecs_list_lock);

	return found;
}

struct ztcpkm_ecs *find_ztcpkm_ecs(pid_t pid)
{
	struct list_head *pos, *next;
	struct ztcpkm_ecs *tmp = NULL, *ecs = NULL;

	mutex_lock(&ecs_list_lock);
	list_for_each_safe(pos, next, &ztcpkm_ecs_list) {
		tmp = list_entry(pos, struct ztcpkm_ecs, list);
		if (tmp && (tmp->pid == pid)) {
			ecs = tmp;
			break;
		}
	}
	mutex_unlock(&ecs_list_lock);

	return ecs;
}

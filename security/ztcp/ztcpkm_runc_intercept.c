// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/binfmts.h>
#include <linux/fs.h>
#include <linux/highmem.h> // k[un]map_*
#include <linux/slab.h>
#include <linux/version.h>

#include "ztcpkm_ecs.h"
#include "ztcpkm_netlink.h"
#include "ztcpkm_runc_intercept.h"
#include "ztcpkm_config.h"
#include "ztcputil.h"

bool ztcpkm_runc_detection(const char *pathname, struct file *file)
{
	int i;
	bool result = false;

	Z_PTR_CHECK_RETURN_VALUE(pathname, false, __func__);
	Z_PTR_CHECK_RETURN_VALUE(file, false, __func__);

#ifdef DEBUG
	pr_info("pathname: %s\n", pathname);
#endif

	mutex_lock(&zconfig_lock);

	for (i = 0; i < zconfig.num_of_runc_path; i++) {
#ifdef DEBUG
		pr_info("zconfig.runc_path[%d] = \"%s\"\n", i, zconfig.runc_path[i]);
#endif
		if (strncmp(pathname,
			    zconfig.runc_path[i],
			    strlen(zconfig.runc_path[i])) == 0) {
			result = true;
			break;
		}
    }

	mutex_unlock(&zconfig_lock);

	return result;
}

static int _ztcpkm_get_runc_args(struct linux_binprm *bprm, char **arg_buf)
{
	// ref: https://github.com/torvalds/linux/blob/ef5f68cc1f829b492b19cd4df5af4454aa816b93/fs/exec.c#L198
	int rc;
	struct page *arg_page;
	unsigned int gup_flags = 0;
	unsigned long page_off;
	char *runc_argp;
	int runc_argc;
	unsigned long runc_argp_off;

	Z_PTR_CHECK_RETURN_VALUE(bprm, -EINVAL, __func__);

	page_off = bprm->p & ~PAGE_MASK;

	// lock bprm->mm to get data pages
	mmap_read_lock(bprm->mm);
	rc = get_user_pages_remote(
		bprm->mm,
		bprm->p,
		1,
		gup_flags,
		&arg_page,
		NULL);
	mmap_read_unlock(bprm->mm);

	if (rc <= 0) {
		ztcpkm_log_err("get_user_page_remote failed, err = %d\n", rc);
		return -1;
	} else {
		// TODO: what is data page is larger and 4K (PAGE_SIZE)?
		*arg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!arg_buf) {
			ztcpkm_log_err("arg_buf kzalloc error\n");
			put_page(arg_page);
			return -1;
		} else {
			sprintf(*arg_buf, "%d", task_pid_nr(current));

			runc_argp = kmap(arg_page);
			runc_argc = bprm->argc;
			runc_argp_off = page_off;
			while (runc_argc && page_off < PAGE_SIZE) {
				if (runc_argp[page_off++] == '\0') {
					// add space to separate arguments
					if (strlen(*arg_buf) > 0)
						strcat(*arg_buf, " ");
					strcat(*arg_buf, &runc_argp[runc_argp_off]);
					runc_argp_off = page_off;
					runc_argc--;
				}
			}
			kunmap(arg_page);
			put_page(arg_page);
#ifdef DEBUG
			pr_info("  arg_buf: %s\n", *arg_buf);
#endif
		}
	}

	return 0;
}

int handle_ztcpsa_message(const char *msg)
{
	// msg format: pid [pass|fail]
	char *cur, *token;
	char *const delim = " ";
	pid_t pid_to_handle;
	struct ztcpkm_ecs *ecs;

	cur = (char *)msg;
	token = strsep(&cur, delim);
	if (kstrtouint(token, 10, &pid_to_handle) == 0) {
		ecs = find_ztcpkm_ecs(pid_to_handle);
		if (ecs) {
			if (strcmp(cur, "pass") == 0)
				atomic_set(&ecs->result, 0);
			complete(&ecs->ztcpsa_responded);
			return 0;
		}
	}

	return -1;
}

// Whitelist check for crun commands
//
// Example of command requiring verification:
//   create:  /usr/bin/crun --systemd-cgroup --log-format=json create --bundle ... <container-id>
//
// Example of command allowed without verification:
//   kill:    /usr/bin/crun kill <container-id> 15
//
// Example of denied command
//   exec:    /usr/bin/crun exec
//
enum crun_args_check_result _runc_args_check(const char *arg_buf)
{
	unsigned int len = strlen(arg_buf);
	pr_info("args: %s\n", arg_buf);

	// In some corner cases order of allow/needs verify may matter

	// Commands requiring ztcpsa verification
	if (strnstr(arg_buf, " create ", len) != NULL)
		return CRUN_ARGS_NEEDS_VERIFICATION;

	if (strnstr(arg_buf, " start ", len) != NULL)
		return CRUN_ARGS_NEEDS_VERIFICATION;

	if (strnstr(arg_buf, " resume ", len) != NULL)
		return CRUN_ARGS_NEEDS_VERIFICATION;

	// Commands allowed without verification
	if (strnstr(arg_buf, " kill ", len) != NULL)
		return CRUN_ARGS_ALLOWED;

	if (strnstr(arg_buf, " delete ", len) != NULL)
		return CRUN_ARGS_ALLOWED;

	if (strnstr(arg_buf, " pause ", len) != NULL)
		return CRUN_ARGS_ALLOWED;

	if (strnstr(arg_buf, " --version", len) != NULL)
		return CRUN_ARGS_ALLOWED;

	// Everything else is denied
	return CRUN_ARGS_DENIED;
}

enum runc_interception_result ztcpkm_runc_interception(struct linux_binprm *bprm)
{
	int rc;
	char *arg_buf = NULL;
	struct ztcpkm_ecs *ecs;
	enum runc_interception_result rir = RIR_ERROR;

	rc = _ztcpkm_get_runc_args(bprm, &arg_buf);
	if (rc == 0) {
		enum crun_args_check_result check_result;
#ifdef DEBUG
		pr_info("arg_buf: %s\n", arg_buf);
#endif
		check_result = _runc_args_check(arg_buf);
		switch (check_result) {
		case CRUN_ARGS_ALLOWED:
			Z_KFREE_NULL(arg_buf);
			rir = RIR_IMAGE_COMMAND_ALLOWED;
			break;
		case CRUN_ARGS_NEEDS_VERIFICATION:
			ecs = kmalloc(sizeof(*ecs), GFP_KERNEL);
			if (ecs) {
				ecs->pid = task_pid_nr(current);
				init_completion(&ecs->ztcpsa_responded);
				atomic_set(&ecs->result, -1);

				add_ztcpkm_ecs(ecs);

				if (ztcpkm_netlink_send(arg_buf) == true) {
					wait_for_completion_interruptible(&ecs->ztcpsa_responded);
					if (atomic_read(&ecs->result) == 0)
						rir = RIR_IMAGE_VERIFICATION_SUCCESS;
					else
						rir = RIR_IMAGE_VERIFICATION_FAIL;
				} else {
					ztcpkm_log_err("failed to talk to ztcpsa\n");
					rir = RIR_ERROR;
				}

				del_ztcpkm_ecs(ecs);
				Z_KFREE_NULL(ecs);
			} else {
				ztcpkm_log_err("failed to allocate ecs\n");
			}

			Z_KFREE_NULL(arg_buf);
			break;
		case CRUN_ARGS_DENIED:
		default:
			Z_KFREE_NULL(arg_buf);
			rir = RIR_IMAGE_COMMAND_DENIED;
			break;
		}
	}

        return rir;
}

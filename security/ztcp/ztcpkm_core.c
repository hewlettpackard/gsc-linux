// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/binfmts.h> // linux_binprm
#include <linux/cred.h> // cred
#include <linux/fs.h>
#ifdef ___ZTCPKM_PROFILING
#include <linux/ktime.h>
#endif
#include <linux/mm.h>
#include <linux/path.h>
#include <linux/sched.h> // task
#include <linux/version.h>

#include "ztcpkm_config.h"
#include "ztcpkm_core.h"
#include "ztcputil.h"

#include "ztcpkm_runc_intercept.h"

#define SKIP_MESSAGE "Failed to get pathname in %s operation! Skip immutability check!\n"

// FPN stands for full pathname
static int _ztcpkm_getFPNFromPath(const struct path *path, char **pathname, char **page)
{
	char *tmp_page;
	char *tmp_pathname;

	tmp_page = (char *)__get_free_page(GFP_KERNEL);
	if (!tmp_page) {
		ztcpkm_log_err("Failed to allocate memory for resolving full pathname\n");
		return -ENOMEM;
	}

	path_get(path);
	tmp_pathname = d_path(path, tmp_page, PAGE_SIZE);
	path_put(path);

	if (IS_ERR(tmp_pathname)) {
		pr_debug("pathname error\n");
		Z_FREEPAGE_NULL(tmp_page)
		return -EINVAL;
	}

	pr_debug("full_pathname: %s\n", tmp_pathname);
	*pathname = tmp_pathname;
	*page = tmp_page; // caller is responsible to free

	return 0;
}

int ztcpkm_security_bprm_check(struct linux_binprm *bprm)
{
	int rc;
	bool block;
	char *tmp_page;
	char *pathname;

#ifdef ___ZTCPKM_PROFILING
	struct timespec64 start, end;
	ktime_get_real_ts64(&start);
#endif

        Z_PTR_CHECK_RETURN_VALUE(bprm, -EINVAL, "invalid bprm")

        pr_debug("%s\n", __func__);

	block = false;

        rc = _ztcpkm_getFPNFromPath(&bprm->file->f_path, &pathname, &tmp_page);
	if (rc == 0) {
		pr_debug("  pathname: %s\n", pathname);

		/* Check runc detection before freeing tmp_page */
		if (ztcpkm_runc_detection(pathname, bprm->file) == true) {
			enum runc_interception_result rir;
			rir = ztcpkm_runc_interception(bprm);
			switch (rir) {
			case RIR_NONIMAGE_COMMAND:
				break;
			case RIR_IMAGE_COMMAND_ALLOWED:
				ztcpkm_log_info("crun command allowed without verification\n");
				break;
			case RIR_IMAGE_COMMAND_DENIED:
				ztcpkm_log_err("crun command denied\n");
				block = true;
				break;
			case RIR_IMAGE_VERIFICATION_SUCCESS:
				ztcpkm_log_info("container image verification succeeded\n");
				break;
			case RIR_IMAGE_VERIFICATION_FAIL:
				ztcpkm_log_err("container image verification failed\n");
				block = true;
				break;
			case RIR_ERROR:
				ztcpkm_log_err("rir error\n");
				block = true;
				break;
			default:
				ztcpkm_log_err("unknown rir\n");
				block = true;
				break;
			}
		}

		Z_FREEPAGE_NULL(tmp_page)
	} else {
		ztcpkm_log_warn(SKIP_MESSAGE, "exec");
        }

#ifdef ___ZTCPKM_PROFILING
	ktime_get_real_ts64(&end);
	pr_info("[profiling] ztcpkm_security_bprm_check took %lld ns\n", timespec64_to_ns(&end) - timespec64_to_ns(&start));
#endif

	return (block == true) ? -EACCES : 0;
}

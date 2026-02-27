/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#ifndef ___ZTCPKM_RUNC_INTERCEPT_H
#define ___ZTCPKM_RUNC_INTERCEPT_H

#include <linux/binfmts.h>
#include <linux/fs.h>

enum runc_interception_result {
	RIR_NONIMAGE_COMMAND,
	RIR_IMAGE_COMMAND_ALLOWED,
	RIR_IMAGE_COMMAND_DENIED,
	RIR_IMAGE_VERIFICATION_SUCCESS,
	RIR_IMAGE_VERIFICATION_FAIL,
	RIR_ERROR
};

enum crun_args_check_result {
	CRUN_ARGS_DENIED,
	CRUN_ARGS_ALLOWED,
	CRUN_ARGS_NEEDS_VERIFICATION
};

bool ztcpkm_runc_detection(const char *pathname, struct file *file);
enum runc_interception_result ztcpkm_runc_interception(struct linux_binprm *bprm);
int handle_ztcpsa_message(const char *msg);

#endif

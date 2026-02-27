# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_SECURITY_ZTCP) += ztcp.o

ccflags-y += -D___ZTCPKM_COMMIT=\"openbmc_release\"
ztcp-y := \
	ztcpkm.o \
	ztcpkm_lsm.o \
	ztcpkm_core.o \
	ztcputil.o \
	ztcpkm_netlink.o \
	ztcpkm_ecs.o \
	ztcpkm_runc_intercept.o \
	ztcpkm_config.o

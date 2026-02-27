// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2026 Hewlett Packard Enterprise Development LP */

#include <linux/netlink.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

#include "ztcpkm_netlink.h"
#include "ztcpkm_runc_intercept.h"
#include "ztcputil.h"

static struct sock *nl_sk;
static DEFINE_MUTEX(nl_lock);
static atomic_t ztcpsa_pid = ATOMIC_INIT(0);

static void ztcpkm_netlink_recv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	int pid;
	char *msg;

	Z_PTR_CHECK_RETURN_NONE(skb, __func__)

	if (skb->len < sizeof(struct nlmsghdr)) {
		ztcpkm_log_kernel("invalid skb size\n");
		return;
	}

	nlh = (struct nlmsghdr *)skb->data;
	Z_PTR_CHECK_RETURN_NONE(nlh, __func__)

	pid = nlh->nlmsg_pid; /* pid of sending process */

	msg = NLMSG_DATA(nlh);
	Z_PTR_CHECK_RETURN_NONE(nlh, __func__)

	// TODO: relax checking during development, enhance in the future
	//if (atomic_read(&ztcpsa_pid) == 0 && (strncmp(msg, "zhtschpaskae", 12) == 0)) {
	if (strncmp(msg, "zhtschpaskae", 12) == 0) {
		pr_info("ztcpsa handshake message from pid %d\n", pid);
		atomic_set(&ztcpsa_pid, pid);
		ztcpkm_netlink_send("zhtschpaskae confirmed\0");
	}

	if (atomic_read(&ztcpsa_pid) == 0) {
		ztcpkm_log_kernel("not handshaked yet - pid %d: \"%s\"\n", pid, msg);
		return;
	}

	if (atomic_read(&ztcpsa_pid) != pid) {
		ztcpkm_log_kernel("invalid ztcpsa pid %d: \"%s\"\n", pid, msg);
		return;
	}

	if (atomic_read(&ztcpsa_pid) && (strncmp(msg, "zltecapvsea", 5) == 0)) {
		pr_debug("ztcpsa leave message from pid %d\n", pid);
		ztcpkm_netlink_send("zltecapvsea confirmed\0");
		atomic_set(&ztcpsa_pid, 0);
	}

	pr_debug("received from pid %d: %s\n", pid, msg);
	handle_ztcpsa_message(msg);
}

bool ztcpkm_netlink_send(const char *msg)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int msg_size;
	int res;

	Z_PTR_CHECK_RETURN_VALUE(nl_sk, false, __func__)
	Z_PTR_CHECK_RETURN_VALUE(msg, false, __func__)

	msg_size = strlen(msg);

	if (atomic_read(&ztcpsa_pid) == 0)  {
		ztcpkm_log_kernel("not handshaked yet\n");
		return false;
	}

	skb = nlmsg_new(NLMSG_ALIGN(msg_size + 1), GFP_KERNEL);
	Z_PTR_CHECK_RETURN_VALUE(skb, false, "skb allocation failure")

	nlh = nlmsg_put(skb, 0, 1, NLMSG_DONE, msg_size + 1, 0);
	if (!nlh) {
		kfree_skb(skb);
		ztcpkm_log_kernel("nlmsg_put error\n");
		return false;
	}
	memcpy(nlmsg_data(nlh), (const char *)msg, msg_size + 1);

	mutex_lock(&nl_lock);
	res = nlmsg_unicast(nl_sk, skb, atomic_read(&ztcpsa_pid));
	mutex_unlock(&nl_lock);

	if (res < 0) {
		ztcpkm_log_kernel("nlmsg_unicast error: %d\n", res);
		return false;
	}

	return true;
}

bool ztcpkm_netlink_init(void)
{
	struct netlink_kernel_cfg nl_cfg = {
		.input = ztcpkm_netlink_recv_msg,
	};

	nl_sk = netlink_kernel_create(&init_net, NETLINK_ZTCP, &nl_cfg);
	Z_PTR_CHECK_RETURN_VALUE(nl_sk, false, "error creating netlink socket")

	return true;
}

bool ztcpkm_netlink_fini(void)
{
	Z_PTR_CHECK_RETURN_VALUE(nl_sk, false, "unable to release a null netlink socket")

	netlink_kernel_release(nl_sk);
	nl_sk = NULL;

	return true;
}

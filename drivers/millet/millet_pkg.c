/*
 * Copyright (c) Xiaomi Technologies Co., Ltd. 2019. All rights reserved.
 *
 * millet_pkg.c - package network activity tracking via netfilter
 * Adapted for kernel 4.14 (MT6853/cannon)
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <linux/millet.h>

#define MAX_REC_UID 64
static atomic_t uid_rec[MAX_REC_UID];

int pkg_stat_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < MAX_REC_UID; i++)
		if (atomic_read(&uid_rec[i]))
			seq_printf(m, "%d\t", atomic_read(&uid_rec[i]));
	seq_printf(m, "\n");

	return 0;
}

static void pkg_add_uid(uid_t uid)
{
	int i, j;
	uid_t inner_uid;

	for (i = 0, j = MAX_REC_UID; i < MAX_REC_UID; i++) {
		inner_uid = atomic_read(&uid_rec[i]);
		if (inner_uid == 0 && j == MAX_REC_UID)
			j = i;
		else if (inner_uid == uid)
			goto out;
	}

	if (j < MAX_REC_UID)
		atomic_set(&uid_rec[j], uid);
	else
		pr_err("%s : add uid:%d failed (full)!\n", __func__, uid);

out:
	return;
}

static int pkg_sendmsg(struct task_struct *tsk,
		struct millet_data *data, struct millet_sock *sk)
{
	int ret;

	if (!sk || !data || !tsk) {
		pr_err("%s input invalid\n", __func__);
		return RET_ERR;
	}

	data->msg_type = MSG_TO_USER;
	data->owner = PKG_TYPE;
	data->uid = data->mod.k_priv.pkg.pkg_owner;
	ret = millet_sendto_user(tsk, data, sk);

	return ret;
}

static void pkg_del_uid(uid_t uid)
{
	int i;
	uid_t inner_uid;

	for (i = 0; i < MAX_REC_UID; i++) {
		inner_uid = (uid_t) atomic_read(&uid_rec[i]);
		if (inner_uid == uid) {
			atomic_set(&uid_rec[i], 0);
			break;
		}
	}
}

static void pkg_clear_all(void)
{
	int i;

	for (i = 0; i < MAX_REC_UID; i++)
		atomic_set(&uid_rec[i], 0);
}

static int find_and_clear_uid(uid_t uid)
{
	int i;
	uid_t inner_uid;

	for (i = 0; i < MAX_REC_UID; i++) {
		inner_uid = atomic_read(&uid_rec[i]);
		if (unlikely(inner_uid == uid)) {
			if (atomic_cmpxchg(&uid_rec[i], uid, 0) == uid)
				return 1;
			break;
		}
	}

	return 0;
}

static void pkg_recv_hook(void *data, unsigned int len)
{
	struct millet_userconf *payload = (struct millet_userconf *) data;

	switch (payload->mod.u_priv.pkg.cmd) {
	case ADD_UID:
		pkg_add_uid(payload->mod.u_priv.pkg.uid);
		break;
	case DEL_UID:
		pkg_del_uid(payload->mod.u_priv.pkg.uid);
		break;
	case CLEAR_ALL_UID:
		pkg_clear_all();
		break;
	default:
		break;
	}
}

static void pkg_init_millet(struct millet_sock *sk)
{
	if (sk)
		sk->mod[PKG_TYPE].monitor = PKG_TYPE;
}

static uid_t __sock_i_uid(struct sock *sk)
{
	if (sk && sk->sk_socket)
		return SOCK_INODE(sk->sk_socket)->i_uid.val;

	return 0;
}

static unsigned int pkg_ip4_in(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct sock *sk;
	struct millet_data data;
	uid_t uid;
	int found;

	if (ip_hdr(skb)->protocol != IPPROTO_TCP)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk == NULL || !sk_fullsock(sk))
		return NF_ACCEPT;

	uid = __sock_i_uid(sk);
	if (uid < UID_MIN_VALUE)
		return NF_ACCEPT;

	found = find_and_clear_uid(uid);
	if (!found)
		return NF_ACCEPT;

	memset(&data, 0, sizeof(data));
	data.mod.k_priv.pkg.owner_pid = 0;
	data.mod.k_priv.pkg.pkg_owner = (int) uid;
	if (millet_sendmsg(PKG_TYPE, current, &data) < 0)
		pr_err("%s : up report failed!\n", __func__);

	return NF_ACCEPT;
}

static unsigned int pkg_ip6_in(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct sock *sk;
	struct millet_data data;
	unsigned int thoff = 0;
	unsigned short frag_off = 0;
	int protohdr;
	uid_t uid;
	int found;

	protohdr = ipv6_find_hdr(skb, &thoff, -1, &frag_off, NULL);
	if (protohdr != IPPROTO_TCP)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk == NULL || !sk_fullsock(sk))
		return NF_ACCEPT;

	uid = __sock_i_uid(sk);
	if (uid < UID_MIN_VALUE)
		return NF_ACCEPT;

	found = find_and_clear_uid(uid);
	if (!found)
		return NF_ACCEPT;

	memset(&data, 0, sizeof(data));
	data.mod.k_priv.pkg.owner_pid = 0;
	data.mod.k_priv.pkg.pkg_owner = (int) uid;
	if (millet_sendmsg(PKG_TYPE, current, &data) < 0)
		pr_err("%s : up report failed!\n", __func__);

	return NF_ACCEPT;
}

static unsigned int pkg_ip4_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	return NF_ACCEPT;
}

static unsigned int pkg_ip6_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	return NF_ACCEPT;
}

static struct nf_hook_ops pkg_nf_ops[] = {
	{
		.hook		= pkg_ip4_in,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_SELINUX_LAST + 1,
	},
	{
		.hook		= pkg_ip6_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_SELINUX_LAST + 1,
	},
	{
		.hook		= pkg_ip4_out,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_SELINUX_LAST + 1,
	},
	{
		.hook		= pkg_ip6_out,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_SELINUX_LAST + 1,
	},
};

/* Called from millet_core after the netlink socket is ready */
void __init millet_pkg_setup(void)
{
	int ret;
	struct net *net = &init_net;

	ret = nf_register_net_hooks(net, pkg_nf_ops, ARRAY_SIZE(pkg_nf_ops));
	if (ret < 0) {
		pr_err("nf_register_hooks(millet hooks) error\n");
	} else {
		pr_info("nf_register_hooks(millet hooks) success\n");
		register_millet_hook(PKG_TYPE, pkg_recv_hook, pkg_sendmsg,
			pkg_init_millet);
	}
}

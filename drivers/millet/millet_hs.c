/*
 * Copyright (c) Xiaomi Technologies Co., Ltd. 2019. All rights reserved.
 *
 * millet_hs.c - handshake module for millet frozen framework
 * Adapted for kernel 4.14 (MT6853/cannon)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/millet.h>

static int hs_sendmsg(struct task_struct *tsk,
		struct millet_data *data, struct millet_sock *sk)
{
	int ret = 0;

	if (!sk || !data) {
		pr_err("%s input invalid\n", __func__);
		return RET_ERR;
	}

	data->msg_type = MSG_TO_USER;
	data->owner = HANDSHK_TYPE;
	ret = millet_sendto_user(tsk, data, sk);

	return ret;
}

static void hs_recv_hook(void *data, unsigned int len)
{
	struct millet_data d;

	memset(&d, 0, sizeof(d));
	millet_sendmsg(HANDSHK_TYPE, current, &d);
}

static void hs_init_millet(struct millet_sock *sk)
{
	if (sk)
		sk->mod[HANDSHK_TYPE].monitor = SIG_TYPE;
}

/* Called from millet_core after socket is ready */
void __init millet_hs_setup(void)
{
	pr_info("hs_register_hooks(millet hooks) success\n");
	register_millet_hook(HANDSHK_TYPE, hs_recv_hook, hs_sendmsg,
		hs_init_millet);
}

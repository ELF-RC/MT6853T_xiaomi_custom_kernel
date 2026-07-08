// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Copyright (c) 2017-present, Facebook, Inc.
 */
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>
#include <crypto/internal/scompress.h>

#define ZSTD_DEF_LEVEL	3

struct zstd_ctx {
	void *cwksp;
	void *dwksp;
	zstd_cctx *cctx;
	zstd_dctx *dctx;
};

static int zstd_comp_init(struct zstd_ctx *ctx)
{
	zstd_parameters params = zstd_get_params(ZSTD_DEF_LEVEL, 0);
	size_t wksp_size = zstd_cctx_workspace_bound(&params.cParams);

	/*
	 * zstd 1.5.2's static CCtx requires substantial workspace for
	 * internal structures (match tables, entropy tables, etc).
	 * The estimate from zstd_cctx_workspace_bound is a minimum;
	 * add significant headroom to avoid buffer overflow.
	 */
	wksp_size += 262144; /* 256KB headroom for safety */

	ctx->cwksp = vzalloc(wksp_size);
	if (!ctx->cwksp)
		return -ENOMEM;

	ctx->cctx = zstd_init_cctx(ctx->cwksp, wksp_size);
	if (!ctx->cctx) {
		vfree(ctx->cwksp);
		return -EINVAL;
	}
	return 0;
}

static int zstd_decomp_init(struct zstd_ctx *ctx)
{
	size_t wksp_size = zstd_dctx_workspace_bound();

	/* Add headroom for safety */
	wksp_size += 65536;

	ctx->dwksp = vzalloc(wksp_size);
	if (!ctx->dwksp)
		return -ENOMEM;

	ctx->dctx = zstd_init_dctx(ctx->dwksp, wksp_size);
	if (!ctx->dctx) {
		vfree(ctx->dwksp);
		return -EINVAL;
	}
	return 0;
}

static void zstd_comp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->cwksp);
	ctx->cwksp = NULL;
	ctx->cctx = NULL;
}

static void zstd_decomp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->dwksp);
	ctx->dwksp = NULL;
	ctx->dctx = NULL;
}

static int __zstd_init(void *ctx)
{
	int ret;

	ret = zstd_comp_init(ctx);
	if (ret)
		return ret;
	ret = zstd_decomp_init(ctx);
	if (ret)
		zstd_comp_exit(ctx);
	return ret;
}

static void *zstd_alloc_ctx(struct crypto_scomp *tfm)
{
	struct zstd_ctx *ctx;

	ctx = kzalloc(sizeof(struct zstd_ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	if (__zstd_init(ctx)) {
		kfree(ctx);
		return NULL;
	}

	return ctx;
}

static int zstd_init(struct crypto_tfm *tfm)
{
	return __zstd_init(crypto_tfm_ctx(tfm));
}

static void __zstd_exit(void *ctx)
{
	zstd_comp_exit(ctx);
	zstd_decomp_exit(ctx);
}

static void zstd_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	__zstd_exit(ctx);
	kfree(ctx);
}

static void zstd_exit(struct crypto_tfm *tfm)
{
	__zstd_exit(crypto_tfm_ctx(tfm));
}

static int __zstd_compress(const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen, void *ctx)
{
	struct zstd_ctx *zctx = ctx;
	size_t out_len;

	/*
	 * Use ZSTD_compressCCtx directly instead of zstd_compress_cctx().
	 * The kernel wrapper calls zstd_cctx_init() which calls
	 * ZSTD_CCtx_reset() - this crashes on static CCtx in zstd 1.5.2.
	 * ZSTD_compressCCtx() calls ZSTD_compress_usingDict() which does
	 * NOT call reset, making it safe for static contexts.
	 */
	out_len = ZSTD_compressCCtx(zctx->cctx, dst, *dlen, src, slen,
				    ZSTD_DEF_LEVEL);
	if (zstd_is_error(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int __zstd_decompress(const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int *dlen, void *ctx)
{
	struct zstd_ctx *zctx = ctx;
	size_t out_len;

	/*
	 * Use ZSTD_decompressDCtx directly instead of zstd_decompress_dctx().
	 * The kernel wrapper also calls into the same path, but this ensures
	 * we use the pre-allocated static DCtx, avoiding kmalloc in the
	 * swap read path which would trigger direct reclaim and deadlock
	 * with the GPU shrinker.
	 */
	out_len = ZSTD_decompressDCtx(zctx->dctx, dst, *dlen, src, slen);
	if (zstd_is_error(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int zstd_compress(struct crypto_tfm *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __zstd_compress(src, slen, dst, dlen, crypto_tfm_ctx(tfm));
}

static int zstd_scompress(struct crypto_scomp *tfm, const u8 *src,
			  unsigned int slen, u8 *dst, unsigned int *dlen,
			  void *ctx)
{
	return __zstd_compress(src, slen, dst, dlen, ctx);
}

static int zstd_decompress(struct crypto_tfm *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __zstd_decompress(src, slen, dst, dlen, crypto_tfm_ctx(tfm));
}

static int zstd_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen,
			    void *ctx)
{
	return __zstd_decompress(src, slen, dst, dlen, ctx);
}

static struct crypto_alg alg = {
	.cra_name		= "zstd",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_module		= THIS_MODULE,
	.cra_u			= { .compress = {
	.coa_compress		= zstd_compress,
	.coa_decompress		= zstd_decompress } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= zstd_alloc_ctx,
	.free_ctx		= zstd_free_ctx,
	.compress		= zstd_scompress,
	.decompress		= zstd_sdecompress,
	.base			= {
		.cra_name	= "zstd",
		.cra_driver_name = "zstd-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init zstd_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp);
	if (ret)
		crypto_unregister_alg(&alg);

	return ret;
}

static void __exit zstd_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomp(&scomp);
}

module_init(zstd_mod_init);
module_exit(zstd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zstd Compression Algorithm");
MODULE_ALIAS_CRYPTO("zstd");

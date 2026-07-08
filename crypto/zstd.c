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
};

static int zstd_comp_init(struct zstd_ctx *ctx)
{
	/*
	 * zstd 1.5.2+ uses dynamic allocation internally.
	 * We just need to ensure the context struct exists.
	 * The workspace is managed by zstd_createCCtx / ZSTD_compress.
	 */
	ctx->cwksp = NULL;
	return 0;
}

static int zstd_decomp_init(struct zstd_ctx *ctx)
{
	ctx->dwksp = NULL;
	return 0;
}

static void zstd_comp_exit(struct zstd_ctx *ctx)
{
	ctx->cwksp = NULL;
}

static void zstd_decomp_exit(struct zstd_ctx *ctx)
{
	ctx->dwksp = NULL;
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
	return kzalloc(sizeof(struct zstd_ctx), GFP_KERNEL);
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
	kfree(ctx);
}

static void zstd_exit(struct crypto_tfm *tfm)
{
	__zstd_exit(crypto_tfm_ctx(tfm));
}

static int __zstd_compress(const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;

	out_len = ZSTD_compress(dst, *dlen, src, slen, ZSTD_DEF_LEVEL);
	if (zstd_is_error(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int __zstd_decompress(const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;

	out_len = ZSTD_decompress(dst, *dlen, src, slen);
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Standalone ARM64 NEON GHASH shuffle comparison.
 * Derived from arch/arm64/crypto/ghash-ce-glue.c in Linux 6.18.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <crypto/gf128mul.h>
#include <crypto/ghash.h>
#include <crypto/internal/hash.h>
#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/cpufeature.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/unaligned.h>

struct ghash_neon_key {
	u64 h[2];
};

struct ghash_neon_2x_key {
	struct ghash_neon_key single;
	/* Low, high, cross; unrotated and four byte rotations; H^2, H. */
	u64 powers[3][5][2];
};

static_assert(offsetof(struct ghash_neon_2x_key, powers) == GHASH_BLOCK_SIZE);

struct ghash_neon_4x_key {
	/* Keep the complete two-block context for short calls and tails. */
	struct ghash_neon_2x_key tail;
	/* Low/high/cross; H^4/H^3 then H^2/H; five rotations; two lanes. */
	u64 powers[3][2][5][2];
};

static_assert(offsetof(struct ghash_neon_4x_key, powers) == 256);

struct ghash_neon_8x_key {
	/* Keep the four-block context for calls and tails below eight blocks. */
	struct ghash_neon_4x_key tail;
	/* Low/high/cross; H^8/H^7 through H^2/H; rotations; two lanes. */
	u64 powers[3][4][5][2];
};

static_assert(offsetof(struct ghash_neon_8x_key, powers) == 736);

struct ghash_neon_fused_key {
	struct ghash_neon_key single;
	/* Low/high product sums; unrotated and four byte rotations; K, H. */
	u64 pairs[2][5][2];
};

static_assert(offsetof(struct ghash_neon_fused_key, pairs) == GHASH_BLOCK_SIZE);

struct ghash_neon_umull_key {
	struct ghash_neon_key single;
	u32 pairs[4][4][2];
	u32 cross[4];
};

static_assert(offsetof(struct ghash_neon_umull_key, pairs) == GHASH_BLOCK_SIZE);
static_assert(offsetof(struct ghash_neon_umull_key, cross) == 144);

struct ghash_neon_desc_ctx {
	u64 digest[2];
};

asmlinkage void ghash_neon_shuffle_blocks(unsigned int blocks, u64 digest[],
					  const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_sched_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_gpr_blocks(unsigned int blocks, u64 digest[],
				      const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_blocks(unsigned int blocks, u64 digest[],
				     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_pmull_blocks(unsigned int blocks, u64 digest[],
					   const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_pmull_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_orig_pmull_blocks(unsigned int blocks, u64 digest[],
					     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_orig_pmull13_blocks(unsigned int blocks, u64 digest[],
					       const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_fused_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_reuse_blocks(unsigned int blocks, u64 digest[],
					   const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_load_blocks(unsigned int blocks, u64 digest[],
					  const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_blocks(unsigned int blocks, u64 digest[],
				     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_8x_blocks(unsigned int blocks, u64 digest[],
				     const u8 *src, const u64 h[]);

asmlinkage void ghash_neon_pmul_blocks(unsigned int blocks, u64 digest[],
				       const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_umull_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);

typedef void (*ghash_blocks_fn)(unsigned int blocks, u64 digest[],
				const u8 *src, const u64 h[]);

static __always_inline void ghash_blocks(unsigned int blocks, u64 digest[],
					 const u8 *src,
					 const struct ghash_neon_key *key,
					 ghash_blocks_fn fn)
{
	kernel_neon_begin();
	fn(blocks, digest, src, key->h);
	kernel_neon_end();
}

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_neon_desc_ctx *ctx = shash_desc_ctx(desc);

	*ctx = (struct ghash_neon_desc_ctx){};
	return 0;
}

static __always_inline int ghash_do_update(struct shash_desc *desc,
					   const u8 *src, unsigned int len,
					   ghash_blocks_fn fn)
{
	struct ghash_neon_desc_ctx *ctx = shash_desc_ctx(desc);
	struct ghash_neon_key *key = crypto_shash_ctx(desc->tfm);
	unsigned int blocks = len / GHASH_BLOCK_SIZE;

	/* The block-only shash frontend buffers the remainder. */
	len %= GHASH_BLOCK_SIZE;
	while (blocks) {
		/* Match the stock driver's limit on each SIMD section. */
		unsigned int chunk = min_t(unsigned int, blocks,
					   SZ_64K / GHASH_BLOCK_SIZE);

		ghash_blocks(chunk, ctx->digest, src, key, fn);
		blocks -= chunk;
		src += chunk * GHASH_BLOCK_SIZE;
	}
	return len;
}

static int ghash_export(struct shash_desc *desc, void *out)
{
	struct ghash_neon_desc_ctx *ctx = shash_desc_ctx(desc);
	u8 *dst = out;

	put_unaligned_be64(ctx->digest[1], dst);
	put_unaligned_be64(ctx->digest[0], dst + 8);
	return 0;
}

static int ghash_import(struct shash_desc *desc, const void *in)
{
	struct ghash_neon_desc_ctx *ctx = shash_desc_ctx(desc);
	const u8 *src = in;

	ctx->digest[1] = get_unaligned_be64(src);
	ctx->digest[0] = get_unaligned_be64(src + 8);
	return 0;
}

static __always_inline int ghash_do_finup(struct shash_desc *desc,
					  const u8 *src, unsigned int len,
					  u8 *dst, ghash_blocks_fn fn)
{
	struct ghash_neon_desc_ctx *ctx = shash_desc_ctx(desc);
	struct ghash_neon_key *key = crypto_shash_ctx(desc->tfm);

	if (len) {
		u8 buf[GHASH_BLOCK_SIZE] = {};

		memcpy(buf, src, len);
		ghash_blocks(1, ctx->digest, buf, key, fn);
		memzero_explicit(buf, sizeof(buf));
	}
	return ghash_export(desc, dst);
}

/* Constant function arguments inline to direct calls for all variants. */
static int ghash_shuffle_update(struct shash_desc *desc, const u8 *src,
				unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_shuffle_blocks);
}

static int ghash_shuffle_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_shuffle_blocks);
}

static int ghash_sched_update(struct shash_desc *desc, const u8 *src,
			      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_sched_blocks);
}

static int ghash_sched_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_sched_blocks);
}

static int ghash_gpr_update(struct shash_desc *desc, const u8 *src,
			    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_gpr_blocks);
}

static int ghash_gpr_finup(struct shash_desc *desc, const u8 *src,
			   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_gpr_blocks);
}

static int ghash_2x_update(struct shash_desc *desc, const u8 *src,
			   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_2x_blocks);
}

static int ghash_2x_finup(struct shash_desc *desc, const u8 *src,
			  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_2x_blocks);
}

static int ghash_2x_pmull_update(struct shash_desc *desc, const u8 *src,
				 unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_2x_pmull_blocks);
}

static int ghash_2x_pmull_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_2x_pmull_blocks);
}

static int ghash_pmull_update(struct shash_desc *desc, const u8 *src,
			      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_pmull_blocks);
}

static int ghash_pmull_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_pmull_blocks);
}

static int ghash_orig_pmull_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_orig_pmull_blocks);
}

static int ghash_orig_pmull_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_orig_pmull_blocks);
}

static int ghash_orig_pmull13_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_orig_pmull13_blocks);
}

static int ghash_orig_pmull13_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_orig_pmull13_blocks);
}

static int ghash_fused_update(struct shash_desc *desc, const u8 *src,
			      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_fused_blocks);
}

static int ghash_fused_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_fused_blocks);
}

static int ghash_2x_reuse_update(struct shash_desc *desc, const u8 *src,
				 unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_2x_reuse_blocks);
}

static int ghash_2x_reuse_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_2x_reuse_blocks);
}

static int ghash_2x_load_update(struct shash_desc *desc, const u8 *src,
				unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_2x_load_blocks);
}

static int ghash_2x_load_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_2x_load_blocks);
}

static int ghash_4x_update(struct shash_desc *desc, const u8 *src,
			   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_blocks);
}

static int ghash_4x_finup(struct shash_desc *desc, const u8 *src,
			  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_blocks);
}

static int ghash_8x_update(struct shash_desc *desc, const u8 *src,
			   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_8x_blocks);
}

static int ghash_8x_finup(struct shash_desc *desc, const u8 *src,
			  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_8x_blocks);
}

static int ghash_pmul_update(struct shash_desc *desc, const u8 *src,
			     unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_pmul_blocks);
}

static int ghash_pmul_finup(struct shash_desc *desc, const u8 *src,
			    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_pmul_blocks);
}

static int ghash_umull_update(struct shash_desc *desc, const u8 *src,
			      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_umull_blocks);
}

static int ghash_umull_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_umull_blocks);
}

static void ghash_reflect(u64 h[2], const u8 *inkey)
{
	u64 a, b, carry;

	/* Use the stock driver's shifted/reflected hash-key representation. */
	a = get_unaligned_be64(inkey);
	b = get_unaligned_be64(inkey + 8);
	carry = a >> 63;
	h[0] = (b << 1) | carry;
	h[1] = (a << 1) | (b >> 63);
	if (carry)
		h[1] ^= 0xc200000000000000ULL;
}

static int ghash_setkey(struct crypto_shash *tfm, const u8 *inkey,
			unsigned int keylen)
{
	struct ghash_neon_key *key = crypto_shash_ctx(tfm);

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	ghash_reflect(key->h, inkey);
	return 0;
}

static int ghash_2x_setkey(struct crypto_shash *tfm, const u8 *inkey,
			   unsigned int keylen)
{
	struct ghash_neon_2x_key *key = crypto_shash_ctx(tfm);
	u64 squared[2], a, b;
	unsigned int part, rot;
	be128 h, h2;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	/* Square in GHASH's field representation before reflecting each power. */
	memcpy(&h, inkey, sizeof(h));
	h2 = h;
	gf128mul_lle(&h2, &h);
	ghash_reflect(key->single.h, inkey);
	ghash_reflect(squared, (const u8 *)&h2);

	for (part = 0; part < 3; part++) {
		if (part < 2) {
			a = squared[part];
			b = key->single.h[part];
		} else {
			a = squared[0] ^ squared[1];
			b = key->single.h[0] ^ key->single.h[1];
		}
		for (rot = 0; rot < 5; rot++) {
			key->powers[part][rot][0] = ror64(a, rot * 8);
			key->powers[part][rot][1] = ror64(b, rot * 8);
		}
	}
	memzero_explicit(squared, sizeof(squared));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&h2, sizeof(h2));
	return 0;
}

static int ghash_fused_setkey(struct crypto_shash *tfm, const u8 *inkey,
			      unsigned int keylen)
{
	struct ghash_neon_fused_key *key = crypto_shash_ctx(tfm);
	unsigned int part, rot;
	u64 k[2], h0, h1;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	ghash_reflect(key->single.h, inkey);
	h0 = key->single.h[0];
	h1 = key->single.h[1];
	/*
	 * With t = x^64 and c = 0xc200000000000000, the reflected field
	 * polynomial is t^2 + c*t + 1. Precompute K = H0*(t + c) + H1,
	 * absorbing the first Montgomery fold into the fixed multiplier.
	 * Split the carry-less H0*c product using c's three nonzero bits.
	 */
	k[0] = (h0 << 57) ^ (h0 << 62) ^ (h0 << 63) ^ h1;
	k[1] = (h0 >> 7) ^ (h0 >> 2) ^ (h0 >> 1) ^ h0;
	for (part = 0; part < 2; part++) {
		for (rot = 0; rot < 5; rot++) {
			key->pairs[part][rot][0] = ror64(k[part], rot * 8);
			key->pairs[part][rot][1] =
				ror64(key->single.h[part], rot * 8);
		}
	}
	memzero_explicit(k, sizeof(k));
	return 0;
}

static int ghash_4x_setkey(struct crypto_shash *tfm, const u8 *inkey,
			   unsigned int keylen)
{
	struct ghash_neon_4x_key *key = crypto_shash_ctx(tfm);
	unsigned int part, rot;
	u64 cubed[2], fourth[2], a, b;
	be128 h, power;
	int err;

	/* The prefix is also the short-call/tail implementation's context. */
	err = ghash_2x_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	gf128mul_lle(&power, &h);
	gf128mul_lle(&power, &h);
	ghash_reflect(cubed, (const u8 *)&power);
	gf128mul_lle(&power, &h);
	ghash_reflect(fourth, (const u8 *)&power);
	for (part = 0; part < 3; part++) {
		if (part < 2) {
			a = fourth[part];
			b = cubed[part];
		} else {
			a = fourth[0] ^ fourth[1];
			b = cubed[0] ^ cubed[1];
		}
		for (rot = 0; rot < 5; rot++) {
			key->powers[part][0][rot][0] = ror64(a, rot * 8);
			key->powers[part][0][rot][1] = ror64(b, rot * 8);
		}
		memcpy(key->powers[part][1], key->tail.powers[part],
		       sizeof(key->powers[part][1]));
	}
	memzero_explicit(cubed, sizeof(cubed));
	memzero_explicit(fourth, sizeof(fourth));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static int ghash_8x_setkey(struct crypto_shash *tfm, const u8 *inkey,
			   unsigned int keylen)
{
	struct ghash_neon_8x_key *key = crypto_shash_ctx(tfm);
	unsigned int i, part, rot, pair, lane;
	u64 reflected[2], value;
	be128 h, power;
	int err;

	err = ghash_4x_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 0; i < 8; i++) {
		if (i)
			gf128mul_lle(&power, &h);
		ghash_reflect(reflected, (const u8 *)&power);
		pair = (7 - i) / 2;
		lane = (7 - i) % 2;
		for (part = 0; part < 3; part++) {
			value = part < 2 ? reflected[part] :
				reflected[0] ^ reflected[1];
			for (rot = 0; rot < 5; rot++)
				key->powers[part][pair][rot][lane] =
					ror64(value, rot * 8);
		}
	}
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static int ghash_umull_setkey(struct crypto_shash *tfm, const u8 *inkey,
			      unsigned int keylen)
{
	struct ghash_neon_umull_key *key = crypto_shash_ctx(tfm);
	u32 words[4], pairs[5][2];
	unsigned int i, j, lane;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	ghash_reflect(key->single.h, inkey);
	for (i = 0; i < 4; i++)
		words[i] = key->single.h[i / 2] >> (32 * (i % 2));
	pairs[0][0] = words[0];
	pairs[0][1] = words[1];
	pairs[1][0] = words[2];
	pairs[1][1] = words[3];
	pairs[2][0] = words[0] ^ words[1];
	pairs[2][1] = words[2] ^ words[3];
	pairs[3][0] = words[0] ^ words[2];
	pairs[3][1] = words[1] ^ words[3];
	pairs[4][0] = pairs[2][0] ^ pairs[2][1];
	pairs[4][1] = pairs[4][0];
	for (i = 0; i < ARRAY_SIZE(key->pairs); i++)
		for (j = 0; j < 4; j++)
			for (lane = 0; lane < 2; lane++)
				key->pairs[i][j][lane] = pairs[i][lane] &
					(0x11111111U << j);
	for (j = 0; j < 4; j++) {
		i = (-j) & 3;
		key->cross[j] = pairs[4][0] & (0x11111111U << i);
	}
	memzero_explicit(words, sizeof(words));
	memzero_explicit(pairs, sizeof(pairs));
	return 0;
}

static struct shash_alg ghash_algs[] = {{
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-shuffle",
	/* Explicit comparisons must not displace neon (150) or generic (100). */
	.base.cra_priority	= 99,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_shuffle_update,
	.finup			= ghash_shuffle_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-sched",
	.base.cra_priority	= 98,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_sched_update,
	.finup			= ghash_sched_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-gpr",
	.base.cra_priority	= 97,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_gpr_update,
	.finup			= ghash_gpr_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-2x",
	.base.cra_priority	= 96,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_2x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_2x_update,
	.finup			= ghash_2x_finup,
	.setkey			= ghash_2x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-2x-pmull",
	.base.cra_priority	= 95,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_2x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_2x_pmull_update,
	.finup			= ghash_2x_pmull_finup,
	.setkey			= ghash_2x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-pmull",
	.base.cra_priority	= 94,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_pmull_update,
	.finup			= ghash_pmull_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-orig-pmull",
	.base.cra_priority	= 93,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_orig_pmull_update,
	.finup			= ghash_orig_pmull_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-fused",
	.base.cra_priority	= 92,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_fused_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_fused_update,
	.finup			= ghash_fused_finup,
	.setkey			= ghash_fused_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-2x-reuse",
	.base.cra_priority	= 91,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_2x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_2x_reuse_update,
	.finup			= ghash_2x_reuse_finup,
	.setkey			= ghash_2x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-2x-load",
	.base.cra_priority	= 90,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_2x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_2x_load_update,
	.finup			= ghash_2x_load_finup,
	.setkey			= ghash_2x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x",
	.base.cra_priority	= 89,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_update,
	.finup			= ghash_4x_finup,
	.setkey			= ghash_4x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x",
	.base.cra_priority	= 88,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_update,
	.finup			= ghash_8x_finup,
	.setkey			= ghash_8x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-orig-pmull13",
	.base.cra_priority	= 87,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_orig_pmull13_update,
	.finup			= ghash_orig_pmull13_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-pmul",
	.base.cra_priority	= 86,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_pmul_update,
	.finup			= ghash_pmul_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-umull",
	.base.cra_priority	= 85,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_umull_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_umull_update,
	.finup			= ghash_umull_finup,
	.setkey			= ghash_umull_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}};

static int __init ghash_neon_shuffle_init(void)
{
	if (!cpu_have_named_feature(ASIMD))
		return -ENODEV;

	return crypto_register_shashes(ghash_algs, ARRAY_SIZE(ghash_algs));
}

static void __exit ghash_neon_shuffle_exit(void)
{
	crypto_unregister_shashes(ghash_algs, ARRAY_SIZE(ghash_algs));
}

module_init(ghash_neon_shuffle_init);
module_exit(ghash_neon_shuffle_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("Standalone ARM64 NEON GHASH optimization comparisons");
MODULE_ALIAS_CRYPTO("ghash-neon-shuffle");
MODULE_ALIAS_CRYPTO("ghash-neon-sched");
MODULE_ALIAS_CRYPTO("ghash-neon-gpr");
MODULE_ALIAS_CRYPTO("ghash-neon-2x");
MODULE_ALIAS_CRYPTO("ghash-neon-2x-pmull");
MODULE_ALIAS_CRYPTO("ghash-neon-pmull");
MODULE_ALIAS_CRYPTO("ghash-neon-orig-pmull");
MODULE_ALIAS_CRYPTO("ghash-neon-fused");
MODULE_ALIAS_CRYPTO("ghash-neon-2x-reuse");
MODULE_ALIAS_CRYPTO("ghash-neon-2x-load");
MODULE_ALIAS_CRYPTO("ghash-neon-4x");
MODULE_ALIAS_CRYPTO("ghash-neon-8x");
MODULE_ALIAS_CRYPTO("ghash-neon-orig-pmull13");
MODULE_ALIAS_CRYPTO("ghash-neon-pmul");
MODULE_ALIAS_CRYPTO("ghash-neon-umull");

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

#include "ghash-neon-intrinsics.h"

struct ghash_neon_key {
	u64 h[2];
};

struct ghash_neon_compact_key {
	be128 k;
	u64 h[4][2];
};

static_assert(sizeof(struct ghash_neon_compact_key) == 80);
static_assert(offsetof(struct ghash_neon_compact_key, h) == 16);

struct ghash_neon_4x_schoolbook_key {
	struct ghash_neon_key single;
	/* Full 128-bit rotations 0..8; H^4, H^3, H^2, H; low/high. */
	u64 powers[9][4][2];
};

static_assert(offsetof(struct ghash_neon_4x_schoolbook_key, powers) == 16);

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

struct ghash_neon_k32_key {
	struct ghash_neon_2x_key tail;
	/* Low/high/cross; two diagonal pairs and four crosses; rotations. */
	u64 powers[3][3][3][2];
};

static_assert(offsetof(struct ghash_neon_k32_key, powers) == 256);
static_assert(sizeof(struct ghash_neon_k32_key) == 688);

struct ghash_neon_8x_k32_key {
	struct ghash_neon_k32_key tail;
	u64 powers[3][3][3][2];
};

static_assert(offsetof(struct ghash_neon_8x_k32_key, powers) == 688);
static_assert(sizeof(struct ghash_neon_8x_k32_key) == 1120);

struct ghash_neon_16x_k32_key {
	struct ghash_neon_8x_k32_key tail;
	/* H^12..H^9, then H^16..H^13; the prefix keeps H^8..H. */
	u64 powers[2][3][3][3][2];
};

static_assert(offsetof(struct ghash_neon_16x_k32_key, powers) == 1120);
static_assert(sizeof(struct ghash_neon_16x_k32_key) == 1984);

struct ghash_neon_deep_key {
	struct ghash_neon_2x_key tail;
	u64 operands[3][3][3][2];
};

static_assert(offsetof(struct ghash_neon_deep_key, operands) == 256);
static_assert(sizeof(struct ghash_neon_deep_key) == 688);

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
asmlinkage void ghash_neon_orig_pmull12_blocks(unsigned int blocks, u64 digest[],
					       const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_orig_direct_blocks(unsigned int blocks, u64 digest[],
					      const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_schoolbook_blocks(unsigned int blocks, u64 digest[],
					     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_schoolbook_blocks(unsigned int blocks, u64 digest[],
						const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_schoolbook_reuse_blocks(unsigned int blocks,
						      u64 digest[],
						      const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_schoolbook_load_blocks(unsigned int blocks,
						     u64 digest[],
						     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_fused_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_reuse_blocks(unsigned int blocks, u64 digest[],
					   const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_2x_load_blocks(unsigned int blocks, u64 digest[],
					  const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_blocks(unsigned int blocks, u64 digest[],
				     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_pmull12_blocks(unsigned int blocks, u64 digest[],
					     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_pmull13_blocks(unsigned int blocks, u64 digest[],
					     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_8x_blocks(unsigned int blocks, u64 digest[],
				     const u8 *src, const u64 h[]);

asmlinkage void ghash_neon_pmul_blocks(unsigned int blocks, u64 digest[],
				       const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_umull_blocks(unsigned int blocks, u64 digest[],
					const u8 *src, const u64 h[]);

asmlinkage void ghash_neon_4x_k32_stp_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);
asmlinkage void ghash_neon_4x_k32_stp_pipe_blocks(unsigned int blocks,
						  u64 digest[], const u8 *src,
						  const u64 h[]);
asmlinkage void ghash_neon_8x_k32_stp_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);
asmlinkage void ghash_neon_8x_k32_stp_pipe_blocks(unsigned int blocks,
						  u64 digest[], const u8 *src,
						  const u64 h[]);
asmlinkage void ghash_neon_8x_k32_merge_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src,
					       const u64 h[]);
asmlinkage void ghash_neon_8x_k32_merge_simd_blocks(unsigned int blocks,
						    u64 digest[], const u8 *src,
						    const u64 h[]);
asmlinkage void ghash_neon_8x_k32_merge_ldr_blocks(unsigned int blocks,
						   u64 digest[], const u8 *src,
						   const u64 h[]);
asmlinkage void ghash_neon_8x_k32_merge_early_blocks(unsigned int blocks,
						     u64 digest[], const u8 *src,
						     const u64 h[]);
asmlinkage void ghash_neon_8x_k32_merge_lanes_blocks(unsigned int blocks,
						     u64 digest[], const u8 *src,
						     const u64 h[]);
asmlinkage void ghash_neon_8x_k32_early_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src,
					       const u64 h[]);
asmlinkage void ghash_neon_8x_k32_lanes_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src,
					       const u64 h[]);
asmlinkage void ghash_neon_4x_k32_input_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_cross_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tight_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tight_early_blocks(unsigned int blocks,
						     u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_pipe_blocks(unsigned int blocks,
					      u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_16x_k32_blocks(unsigned int blocks, u64 digest[],
					  const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_16x_k32_merge_blocks(unsigned int blocks,
						u64 digest[], const u8 *src,
						const u64 h[]);
asmlinkage void ghash_neon_8x_k32_no128_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src,
					       const u64 h[]);
asmlinkage void ghash_neon_8x_k32_no64_blocks(unsigned int blocks,
					      u64 digest[], const u8 *src,
					      const u64 h[]);
asmlinkage void ghash_neon_8x_k32_no128_no64_blocks(unsigned int blocks,
						    u64 digest[], const u8 *src,
						    const u64 h[]);
asmlinkage void ghash_neon_8x_k32_no64_early_blocks(unsigned int blocks,
						    u64 digest[], const u8 *src,
						    const u64 h[]);
asmlinkage void ghash_neon_8x_k32_no128_no64_early_blocks(unsigned int blocks,
							  u64 digest[], const u8 *src,
							  const u64 h[]);
asmlinkage void ghash_neon_8x_k32_tbl_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);
asmlinkage void ghash_neon_4x_k32_blocks(unsigned int blocks,
					 u64 digest[], const u8 *src,
					 const u64 h[]);
asmlinkage void ghash_neon_4x_k32_load_blocks(unsigned int blocks,
					      u64 digest[], const u8 *src,
					      const u64 h[]);
asmlinkage void ghash_neon_4x_k32_sched_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src,
					       const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tbl_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tbl_simd_blocks(unsigned int blocks,
						  u64 digest[], const u8 *src,
						  const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tbl_simd_pipe_blocks(unsigned int blocks,
						       u64 digest[],
						       const u8 *src,
						       const u64 h[]);
asmlinkage void ghash_neon_4x_k32_tbl_simd_ldr_blocks(unsigned int blocks,
						      u64 digest[],
						      const u8 *src,
						      const u64 h[]);
asmlinkage void ghash_neon_4x_gpr_reduce_blocks(unsigned int blocks,
						u64 digest[], const u8 *src,
						const u64 h[]);
asmlinkage void ghash_neon_4x_stage_blocks(unsigned int blocks,
					   u64 digest[], const u8 *src,
					   const u64 h[]);

asmlinkage void ghash_neon_4x_k16_blocks(unsigned int blocks,
					 u64 digest[], const u8 *src,
					 const u64 h[]);

asmlinkage void ghash_neon_4x_k16_load_blocks(unsigned int blocks,
					      u64 digest[], const u8 *src,
					      const u64 h[]);

asmlinkage void ghash_neon_4x_k8_blocks(unsigned int blocks,
					u64 digest[], const u8 *src,
					const u64 h[]);

asmlinkage void ghash_neon_4x_k8_load_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);

asmlinkage void ghash_neon_k32_direct_stream_blocks(unsigned int blocks,
						    u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_sched_blocks(unsigned int blocks, u64 digest[],
					    const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_u2_blocks(unsigned int blocks, u64 digest[],
					 const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_u4_blocks(unsigned int blocks, u64 digest[],
					 const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_u2_load_blocks(unsigned int blocks, u64 digest[],
					      const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_u4_load_blocks(unsigned int blocks, u64 digest[],
					      const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_direct_blocks(unsigned int blocks, u64 digest[],
					     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_direct_u4_load_blocks(unsigned int blocks, u64 digest[],
						     const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_packed_sched_blocks(unsigned int blocks, u64 digest[],
						   const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_k32_blocks(unsigned int blocks,
				      u64 digest[], const u8 *src,
				      const u64 h[]);

asmlinkage void ghash_neon_k32_packed_blocks(unsigned int blocks,
					     u64 digest[], const u8 *src,
					     const u64 h[]);

asmlinkage void ghash_neon_4x_k32_compact_blocks(unsigned int blocks,
						 u64 digest[], const u8 *src,
						 const u64 h[]);

asmlinkage void ghash_neon_4x_k32_stack288_blocks(unsigned int blocks,
						  u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_stack144_blocks(unsigned int blocks,
						  u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_regs32_blocks(unsigned int blocks,
						u64 digest[], const u8 *src, const u64 h[]);
asmlinkage void ghash_neon_4x_k32_regs0_blocks(unsigned int blocks,
					       u64 digest[], const u8 *src, const u64 h[]);

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

static int ghash_intrinsics_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_intrinsics_blocks);
}

static int ghash_intrinsics_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_intrinsics_blocks);
}

static int ghash_orig_pmull12_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_orig_pmull12_blocks);
}

static int ghash_orig_pmull12_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_orig_pmull12_blocks);
}

static int ghash_orig_direct_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_orig_direct_blocks);
}

static int ghash_orig_direct_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_orig_direct_blocks);
}

static int ghash_schoolbook_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_schoolbook_blocks);
}

static int ghash_schoolbook_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_schoolbook_blocks);
}

static int ghash_4x_schoolbook_update(struct shash_desc *desc, const u8 *src,
				      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_schoolbook_blocks);
}

static int ghash_4x_schoolbook_finup(struct shash_desc *desc, const u8 *src,
				     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_schoolbook_blocks);
}

static int ghash_4x_schoolbook_reuse_update(struct shash_desc *desc, const u8 *src,
					    unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_schoolbook_reuse_blocks);
}

static int ghash_4x_schoolbook_reuse_finup(struct shash_desc *desc, const u8 *src,
					   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_schoolbook_reuse_blocks);
}

static int ghash_4x_schoolbook_load_update(struct shash_desc *desc, const u8 *src,
					   unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_schoolbook_load_blocks);
}

static int ghash_4x_schoolbook_load_finup(struct shash_desc *desc, const u8 *src,
					  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_schoolbook_load_blocks);
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

static int ghash_4x_pmull12_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_pmull12_blocks);
}

static int ghash_4x_pmull12_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_pmull12_blocks);
}

static int ghash_4x_pmull13_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_pmull13_blocks);
}

static int ghash_4x_pmull13_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_pmull13_blocks);
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

static int ghash_4x_k32_update(struct shash_desc *desc, const u8 *src,
			       unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_blocks);
}

static int ghash_4x_k32_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k32_blocks);
}

static int ghash_4x_k32_load_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_load_blocks);
}

static int ghash_4x_k32_load_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k32_load_blocks);
}

static int ghash_4x_k32_sched_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_sched_blocks);
}

static int ghash_4x_k32_sched_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k32_sched_blocks);
}

static int ghash_4x_k32_tbl_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_tbl_blocks);
}

static int ghash_4x_k32_tbl_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k32_tbl_blocks);
}

static int ghash_8x_k32_merge_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_merge_blocks);
}

static int ghash_8x_k32_merge_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_merge_blocks);
}

static int ghash_8x_k32_merge_simd_update(struct shash_desc *desc, const u8 *src,
					  unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_merge_simd_blocks);
}

static int ghash_8x_k32_merge_simd_finup(struct shash_desc *desc, const u8 *src,
					 unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_merge_simd_blocks);
}

static int ghash_8x_k32_merge_ldr_update(struct shash_desc *desc, const u8 *src,
					 unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_merge_ldr_blocks);
}

static int ghash_8x_k32_merge_ldr_finup(struct shash_desc *desc, const u8 *src,
					unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_merge_ldr_blocks);
}

static int ghash_8x_k32_merge_early_update(struct shash_desc *desc, const u8 *src,
					   unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_merge_early_blocks);
}

static int ghash_8x_k32_merge_early_finup(struct shash_desc *desc, const u8 *src,
					  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_merge_early_blocks);
}

static int ghash_8x_k32_merge_lanes_update(struct shash_desc *desc, const u8 *src,
					   unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_merge_lanes_blocks);
}

static int ghash_8x_k32_merge_lanes_finup(struct shash_desc *desc, const u8 *src,
					  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_merge_lanes_blocks);
}

static int ghash_8x_k32_early_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_early_blocks);
}

static int ghash_8x_k32_early_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_early_blocks);
}

static int ghash_8x_k32_lanes_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_lanes_blocks);
}

static int ghash_8x_k32_lanes_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_lanes_blocks);
}

static int ghash_4x_k32_input_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_input_blocks);
}

static int ghash_4x_k32_input_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			       ghash_neon_4x_k32_input_blocks);
}

static int ghash_4x_k32_cross_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_cross_blocks);
}

static int ghash_4x_k32_cross_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			       ghash_neon_4x_k32_cross_blocks);
}

static int ghash_4x_k32_tight_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_tight_blocks);
}

static int ghash_4x_k32_tight_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			       ghash_neon_4x_k32_tight_blocks);
}

static int ghash_4x_k32_tight_early_update(struct shash_desc *desc, const u8 *src,
					   unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_tight_early_blocks);
}

static int ghash_4x_k32_tight_early_finup(struct shash_desc *desc, const u8 *src,
					  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			       ghash_neon_4x_k32_tight_early_blocks);
}

static int ghash_4x_k32_pipe_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_pipe_blocks);
}

static int ghash_4x_k32_pipe_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			       ghash_neon_4x_k32_pipe_blocks);
}

static int ghash_16x_k32_update(struct shash_desc *desc, const u8 *src,
				unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_16x_k32_blocks);
}

static int ghash_16x_k32_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_16x_k32_blocks);
}

static int ghash_16x_k32_merge_update(struct shash_desc *desc, const u8 *src,
				      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_16x_k32_merge_blocks);
}

static int ghash_16x_k32_merge_finup(struct shash_desc *desc, const u8 *src,
				     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_16x_k32_merge_blocks);
}

static int ghash_8x_k32_no128_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_no128_blocks);
}

static int ghash_8x_k32_no128_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_no128_blocks);
}

static int ghash_8x_k32_no64_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_no64_blocks);
}

static int ghash_8x_k32_no64_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_no64_blocks);
}

static int ghash_8x_k32_no128_no64_update(struct shash_desc *desc, const u8 *src,
					  unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_no128_no64_blocks);
}

static int ghash_8x_k32_no128_no64_finup(struct shash_desc *desc, const u8 *src,
					 unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_no128_no64_blocks);
}

static int ghash_8x_k32_no64_early_update(struct shash_desc *desc, const u8 *src,
					  unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_no64_early_blocks);
}

static int ghash_8x_k32_no64_early_finup(struct shash_desc *desc, const u8 *src,
					 unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_no64_early_blocks);
}

static int ghash_8x_k32_no128_no64_early_update(struct shash_desc *desc, const u8 *src,
						unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_8x_k32_no128_no64_early_blocks);
}

static int ghash_8x_k32_no128_no64_early_finup(struct shash_desc *desc, const u8 *src,
					       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_no128_no64_early_blocks);
}

static int ghash_8x_k32_tbl_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_8x_k32_tbl_blocks);
}

static int ghash_8x_k32_tbl_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_8x_k32_tbl_blocks);
}

static int ghash_4x_k32_stp_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_stp_blocks);
}

static int ghash_4x_k32_stp_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_stp_blocks);
}

static int ghash_4x_k32_stp_pipe_update(struct shash_desc *desc, const u8 *src,
					unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_stp_pipe_blocks);
}

static int ghash_4x_k32_stp_pipe_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_stp_pipe_blocks);
}

static int ghash_8x_k32_stp_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_8x_k32_stp_blocks);
}

static int ghash_8x_k32_stp_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_stp_blocks);
}

static int ghash_8x_k32_stp_pipe_update(struct shash_desc *desc, const u8 *src,
					unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_8x_k32_stp_pipe_blocks);
}

static int ghash_8x_k32_stp_pipe_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_8x_k32_stp_pipe_blocks);
}

static int ghash_4x_k32_tbl_simd_update(struct shash_desc *desc, const u8 *src,
					unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_tbl_simd_blocks);
}

static int ghash_4x_k32_tbl_simd_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_tbl_simd_blocks);
}

static int ghash_4x_k32_tbl_simd_pipe_update(struct shash_desc *desc, const u8 *src,
					     unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_tbl_simd_pipe_blocks);
}

static int ghash_4x_k32_tbl_simd_pipe_finup(struct shash_desc *desc, const u8 *src,
					    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_tbl_simd_pipe_blocks);
}

static int ghash_4x_k32_tbl_simd_ldr_update(struct shash_desc *desc, const u8 *src,
					    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k32_tbl_simd_ldr_blocks);
}

static int ghash_4x_k32_tbl_simd_ldr_finup(struct shash_desc *desc, const u8 *src,
					   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_tbl_simd_ldr_blocks);
}

static int ghash_4x_gpr_reduce_update(struct shash_desc *desc, const u8 *src,
				      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_gpr_reduce_blocks);
}

static int ghash_4x_gpr_reduce_finup(struct shash_desc *desc, const u8 *src,
				     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_gpr_reduce_blocks);
}

static int ghash_4x_stage_update(struct shash_desc *desc, const u8 *src,
				 unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_stage_blocks);
}

static int ghash_4x_stage_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_stage_blocks);
}

static int ghash_4x_k16_update(struct shash_desc *desc, const u8 *src,
			       unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k16_blocks);
}

static int ghash_4x_k16_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k16_blocks);
}

static int ghash_4x_k16_load_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k16_load_blocks);
}

static int ghash_4x_k16_load_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k16_load_blocks);
}

static int ghash_4x_k8_update(struct shash_desc *desc, const u8 *src,
			      unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k8_blocks);
}

static int ghash_4x_k8_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k8_blocks);
}

static int ghash_4x_k8_load_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_4x_k8_load_blocks);
}

static int ghash_4x_k8_load_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_4x_k8_load_blocks);
}

static int ghash_4x_k32_stack288_update(struct shash_desc *desc, const u8 *src,
					unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_stack288_blocks);
}

static int ghash_4x_k32_stack288_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_stack288_blocks);
}

static int ghash_4x_k32_stack144_update(struct shash_desc *desc, const u8 *src,
					unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_stack144_blocks);
}

static int ghash_4x_k32_stack144_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_stack144_blocks);
}

static int ghash_4x_k32_regs32_update(struct shash_desc *desc, const u8 *src,
				      unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_regs32_blocks);
}

static int ghash_4x_k32_regs32_finup(struct shash_desc *desc, const u8 *src,
				     unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_regs32_blocks);
}

static int ghash_4x_k32_regs0_update(struct shash_desc *desc, const u8 *src,
				     unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_regs0_blocks);
}

static int ghash_4x_k32_regs0_finup(struct shash_desc *desc, const u8 *src,
				    unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_regs0_blocks);
}

static int ghash_4x_k32_compact_update(struct shash_desc *desc, const u8 *src,
				       unsigned int len)
{
	return ghash_do_update(desc, src, len,
			       ghash_neon_4x_k32_compact_blocks);
}

static int ghash_4x_k32_compact_finup(struct shash_desc *desc, const u8 *src,
				      unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst,
			      ghash_neon_4x_k32_compact_blocks);
}

static int ghash_k32_direct_stream_update(struct shash_desc *desc, const u8 *src,
					  unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_direct_stream_blocks);
}

static int ghash_k32_direct_stream_finup(struct shash_desc *desc, const u8 *src,
					 unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_direct_stream_blocks);
}

static int ghash_k32_sched_update(struct shash_desc *desc, const u8 *src,
				  unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_sched_blocks);
}

static int ghash_k32_sched_finup(struct shash_desc *desc, const u8 *src,
				 unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_sched_blocks);
}

static int ghash_k32_u2_update(struct shash_desc *desc, const u8 *src,
			       unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_u2_blocks);
}

static int ghash_k32_u2_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_u2_blocks);
}

static int ghash_k32_u4_update(struct shash_desc *desc, const u8 *src,
			       unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_u4_blocks);
}

static int ghash_k32_u4_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_u4_blocks);
}

static int ghash_k32_u2_load_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_u2_load_blocks);
}

static int ghash_k32_u2_load_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_u2_load_blocks);
}

static int ghash_k32_u4_load_update(struct shash_desc *desc, const u8 *src,
				    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_u4_load_blocks);
}

static int ghash_k32_u4_load_finup(struct shash_desc *desc, const u8 *src,
				   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_u4_load_blocks);
}

static int ghash_k32_direct_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_direct_blocks);
}

static int ghash_k32_direct_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_direct_blocks);
}

static int ghash_k32_direct_u4_load_update(struct shash_desc *desc, const u8 *src,
					   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_direct_u4_load_blocks);
}

static int ghash_k32_direct_u4_load_finup(struct shash_desc *desc, const u8 *src,
					  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_direct_u4_load_blocks);
}

static int ghash_k32_packed_sched_update(struct shash_desc *desc, const u8 *src,
					 unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_packed_sched_blocks);
}

static int ghash_k32_packed_sched_finup(struct shash_desc *desc, const u8 *src,
					unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_packed_sched_blocks);
}

static int ghash_k32_update(struct shash_desc *desc, const u8 *src,
			    unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_blocks);
}

static int ghash_k32_finup(struct shash_desc *desc, const u8 *src,
			   unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_blocks);
}

static int ghash_k32_packed_update(struct shash_desc *desc, const u8 *src,
				   unsigned int len)
{
	return ghash_do_update(desc, src, len, ghash_neon_k32_packed_blocks);
}

static int ghash_k32_packed_finup(struct shash_desc *desc, const u8 *src,
				  unsigned int len, u8 *dst)
{
	return ghash_do_finup(desc, src, len, dst, ghash_neon_k32_packed_blocks);
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

static int ghash_4x_schoolbook_setkey(struct crypto_shash *tfm, const u8 *inkey,
				      unsigned int keylen)
{
	struct ghash_neon_4x_schoolbook_key *key = crypto_shash_ctx(tfm);
	unsigned int i, rot, shift;
	u64 reflected[2], a, b;
	be128 h, power;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	ghash_reflect(key->single.h, inkey);
	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 0; i < 4; i++) {
		if (i)
			gf128mul_lle(&power, &h);
		ghash_reflect(reflected, (const u8 *)&power);
		a = reflected[0];
		b = reflected[1];
		key->powers[0][3 - i][0] = a;
		key->powers[0][3 - i][1] = b;
		for (rot = 1; rot < 8; rot++) {
			shift = rot * 8;
			key->powers[rot][3 - i][0] =
				(a >> shift) | (b << (64 - shift));
			key->powers[rot][3 - i][1] =
				(b >> shift) | (a << (64 - shift));
		}
		key->powers[8][3 - i][0] = b;
		key->powers[8][3 - i][1] = a;
	}
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
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

static int ghash_compact_setkey(struct crypto_shash *tfm, const u8 *inkey,
				unsigned int keylen)
{
	struct ghash_neon_compact_key *key = crypto_shash_ctx(tfm);
	unsigned int i;
	be128 power;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	memcpy(&key->k, inkey, sizeof(key->k));
	power = key->k;
	for (i = 0; i < ARRAY_SIZE(key->h); i++) {
		if (i)
			gf128mul_lle(&power, &key->k);
		ghash_reflect(key->h[i], (const u8 *)&power);
	}
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static void ghash_k32_table(u64 table[3][3][3][2], const u64 reflected[4][2])
{
	unsigned int i, part, group, rot, lane;
	u64 a[4];

	memset(table, 0, sizeof_field(struct ghash_neon_k32_key, powers));
	for (part = 0; part < 3; part++) {
		for (i = 0; i < 4; i++)
			a[i] = part < 2 ? reflected[i][part] :
				reflected[i][0] ^ reflected[i][1];
		for (group = 0; group < 3; group++) {
			for (rot = 0; rot < 3; rot++) {
				for (lane = 0; lane < 4; lane++) {
					u32 v;

					if (group < 2)
						v = a[group * 2 + lane / 2] >>
							((lane % 2) * 32);
					else
						v = (u32)a[lane] ^ (u32)(a[lane] >> 32);
					table[part][group][rot][lane / 2] |=
						(u64)ror32(v, rot * 8) << ((lane % 2) * 32);
				}
			}
		}
	}
	memzero_explicit(a, sizeof(a));
}

static int ghash_k32_setkey(struct crypto_shash *tfm, const u8 *inkey,
			    unsigned int keylen)
{
	struct ghash_neon_k32_key *key = crypto_shash_ctx(tfm);
	unsigned int i;
	u64 reflected[4][2];
	be128 h, power;
	int err;

	err = ghash_2x_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 0; i < 4; i++) {
		if (i)
			gf128mul_lle(&power, &h);
		ghash_reflect(reflected[3 - i], (const u8 *)&power);
	}
	ghash_k32_table(key->powers, reflected);
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static int ghash_8x_k32_setkey(struct crypto_shash *tfm, const u8 *inkey,
			       unsigned int keylen)
{
	struct ghash_neon_8x_k32_key *key = crypto_shash_ctx(tfm);
	u64 reflected[4][2];
	unsigned int i;
	be128 h, power;
	int err;

	err = ghash_k32_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 1; i < 8; i++) {
		gf128mul_lle(&power, &h);
		if (i >= 4)
			ghash_reflect(reflected[7 - i], (const u8 *)&power);
	}
	ghash_k32_table(key->powers, reflected);
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static int ghash_16x_k32_setkey(struct crypto_shash *tfm, const u8 *inkey,
				unsigned int keylen)
{
	struct ghash_neon_16x_k32_key *key = crypto_shash_ctx(tfm);
	u64 reflected[4][2];
	unsigned int i;
	be128 h, power;
	int err;

	err = ghash_8x_k32_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 1; i < 16; i++) {
		gf128mul_lle(&power, &h);
		if (i < 8)
			continue;
		ghash_reflect(reflected[3 - i % 4], (const u8 *)&power);
		if (i % 4 == 3)
			ghash_k32_table(key->powers[(i - 8) / 4], reflected);
	}
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static void ghash_deep_table(u64 table[3][2], const u32 value[4], bool byte)
{
	unsigned int lane;

	for (lane = 0; lane < 4; lane++) {
		u32 v = value[lane];
		u16 cross = v ^ (v >> 16);

		table[0][lane / 2] |= (u64)v << ((lane % 2) * 32);
		table[2][0] |= (u64)cross << (lane * 16);
		if (byte) {
			u32 halves = v ^ (v >> 8);
			u8 all = cross ^ (cross >> 8);

			table[1][0] |= (u64)(halves & 0xff) << (lane * 16);
			table[1][0] |= (u64)((halves >> 16) & 0xff) << (lane * 16 + 8);
			table[2][1] |= (u64)all << (lane * 8);
			table[2][1] |= (u64)all << (lane * 8 + 32);
		} else {
			u32 swap = ((v & 0x00ff00ffU) << 8) |
				((v >> 8) & 0x00ff00ffU);
			u16 swap_cross = (cross >> 8) | (cross << 8);

			table[1][lane / 2] |= (u64)swap << ((lane % 2) * 32);
			table[2][1] |= (u64)swap_cross << (lane * 16);
		}
	}
}

static int ghash_deep_setkey(struct crypto_shash *tfm, const u8 *inkey,
			     unsigned int keylen, bool byte)
{
	struct ghash_neon_deep_key *key = crypto_shash_ctx(tfm);
	unsigned int i, part, group, lane;
	u64 reflected[4][2], a[4];
	u32 value[4];
	be128 h, power;
	int err;

	err = ghash_2x_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(&h, inkey, sizeof(h));
	power = h;
	for (i = 0; i < 4; i++) {
		if (i)
			gf128mul_lle(&power, &h);
		ghash_reflect(reflected[3 - i], (const u8 *)&power);
	}
	memset(key->operands, 0, sizeof(key->operands));
	for (part = 0; part < 3; part++) {
		for (i = 0; i < 4; i++)
			a[i] = part < 2 ? reflected[i][part] :
				reflected[i][0] ^ reflected[i][1];
		for (group = 0; group < 3; group++) {
			for (lane = 0; lane < 4; lane++)
				value[lane] = group < 2 ?
					a[group * 2 + lane / 2] >> ((lane % 2) * 32) :
					(u32)a[lane] ^ (u32)(a[lane] >> 32);
			ghash_deep_table(key->operands[part][group], value, byte);
		}
	}
	memzero_explicit(value, sizeof(value));
	memzero_explicit(a, sizeof(a));
	memzero_explicit(reflected, sizeof(reflected));
	memzero_explicit(&h, sizeof(h));
	memzero_explicit(&power, sizeof(power));
	return 0;
}

static int ghash_k16_setkey(struct crypto_shash *tfm, const u8 *inkey,
			    unsigned int keylen)
{
	return ghash_deep_setkey(tfm, inkey, keylen, false);
}

static int ghash_k8_setkey(struct crypto_shash *tfm, const u8 *inkey,
			   unsigned int keylen)
{
	return ghash_deep_setkey(tfm, inkey, keylen, true);
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
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-orig-pmull12",
	.base.cra_priority	= 84,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_orig_pmull12_update,
	.finup			= ghash_orig_pmull12_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-pmull12",
	.base.cra_priority	= 83,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_pmull12_update,
	.finup			= ghash_4x_pmull12_finup,
	.setkey			= ghash_4x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-pmull13",
	.base.cra_priority	= 82,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_pmull13_update,
	.finup			= ghash_4x_pmull13_finup,
	.setkey			= ghash_4x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-intrinsics",
	.base.cra_priority	= 81,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_intrinsics_update,
	.finup			= ghash_intrinsics_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-orig-direct",
	.base.cra_priority	= 80,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_orig_direct_update,
	.finup			= ghash_orig_direct_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-schoolbook",
	.base.cra_priority	= 79,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_schoolbook_update,
	.finup			= ghash_schoolbook_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-schoolbook",
	.base.cra_priority	= 78,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_schoolbook_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_schoolbook_update,
	.finup			= ghash_4x_schoolbook_finup,
	.setkey			= ghash_4x_schoolbook_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-schoolbook-reuse",
	.base.cra_priority	= 77,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_schoolbook_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_schoolbook_reuse_update,
	.finup			= ghash_4x_schoolbook_reuse_finup,
	.setkey			= ghash_4x_schoolbook_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-schoolbook-load",
	.base.cra_priority	= 76,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_schoolbook_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_schoolbook_load_update,
	.finup			= ghash_4x_schoolbook_load_finup,
	.setkey			= ghash_4x_schoolbook_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32",
	.base.cra_priority	= 75,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_update,
	.finup			= ghash_4x_k32_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-load",
	.base.cra_priority	= 74,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_load_update,
	.finup			= ghash_4x_k32_load_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-sched",
	.base.cra_priority	= 73,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_sched_update,
	.finup			= ghash_4x_k32_sched_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tbl",
	.base.cra_priority	= 72,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tbl_update,
	.finup			= ghash_4x_k32_tbl_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-gpr-reduce",
	.base.cra_priority	= 71,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_gpr_reduce_update,
	.finup			= ghash_4x_gpr_reduce_finup,
	.setkey			= ghash_4x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-stage",
	.base.cra_priority	= 70,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_4x_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_stage_update,
	.finup			= ghash_4x_stage_finup,
	.setkey			= ghash_4x_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k16",
	.base.cra_priority	= 69,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_deep_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k16_update,
	.finup			= ghash_4x_k16_finup,
	.setkey			= ghash_k16_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k16-load",
	.base.cra_priority	= 68,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_deep_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k16_load_update,
	.finup			= ghash_4x_k16_load_finup,
	.setkey			= ghash_k16_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k8",
	.base.cra_priority	= 67,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_deep_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k8_update,
	.finup			= ghash_4x_k8_finup,
	.setkey			= ghash_k8_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k8-load",
	.base.cra_priority	= 66,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_deep_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k8_load_update,
	.finup			= ghash_4x_k8_load_finup,
	.setkey			= ghash_k8_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32",
	.base.cra_priority	= 65,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_update,
	.finup			= ghash_k32_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-packed",
	.base.cra_priority	= 64,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_packed_update,
	.finup			= ghash_k32_packed_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tbl-simd",
	.base.cra_priority	= 63,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tbl_simd_update,
	.finup			= ghash_4x_k32_tbl_simd_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tbl-simd-pipe",
	.base.cra_priority	= 62,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tbl_simd_pipe_update,
	.finup			= ghash_4x_k32_tbl_simd_pipe_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tbl-simd-ldr",
	.base.cra_priority	= 61,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tbl_simd_ldr_update,
	.finup			= ghash_4x_k32_tbl_simd_ldr_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-tbl",
	.base.cra_priority	= 60,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_tbl_update,
	.finup			= ghash_8x_k32_tbl_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-stp",
	.base.cra_priority	= 59,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_stp_update,
	.finup			= ghash_4x_k32_stp_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-stp-pipe",
	.base.cra_priority	= 58,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_stp_pipe_update,
	.finup			= ghash_4x_k32_stp_pipe_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-stp",
	.base.cra_priority	= 57,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_stp_update,
	.finup			= ghash_8x_k32_stp_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-stp-pipe",
	.base.cra_priority	= 56,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_stp_pipe_update,
	.finup			= ghash_8x_k32_stp_pipe_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-merge",
	.base.cra_priority	= 55,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_merge_update,
	.finup			= ghash_8x_k32_merge_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-merge-simd",
	.base.cra_priority	= 54,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_merge_simd_update,
	.finup			= ghash_8x_k32_merge_simd_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-merge-ldr",
	.base.cra_priority	= 53,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_merge_ldr_update,
	.finup			= ghash_8x_k32_merge_ldr_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-merge-early",
	.base.cra_priority	= 52,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_merge_early_update,
	.finup			= ghash_8x_k32_merge_early_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-early",
	.base.cra_priority	= 51,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_early_update,
	.finup			= ghash_8x_k32_early_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-merge-lanes",
	.base.cra_priority	= 50,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_merge_lanes_update,
	.finup			= ghash_8x_k32_merge_lanes_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-lanes",
	.base.cra_priority	= 49,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_lanes_update,
	.finup			= ghash_8x_k32_lanes_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-no128",
	.base.cra_priority	= 48,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_no128_update,
	.finup			= ghash_8x_k32_no128_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-no64",
	.base.cra_priority	= 47,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_no64_update,
	.finup			= ghash_8x_k32_no64_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-no128-no64",
	.base.cra_priority	= 46,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_no128_no64_update,
	.finup			= ghash_8x_k32_no128_no64_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-no64-early",
	.base.cra_priority	= 45,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_no64_early_update,
	.finup			= ghash_8x_k32_no64_early_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-8x-k32-no128-no64-early",
	.base.cra_priority	= 44,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_8x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_8x_k32_no128_no64_early_update,
	.finup			= ghash_8x_k32_no128_no64_early_finup,
	.setkey			= ghash_8x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-16x-k32",
	.base.cra_priority	= 43,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_16x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_16x_k32_update,
	.finup			= ghash_16x_k32_finup,
	.setkey			= ghash_16x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-16x-k32-merge",
	.base.cra_priority	= 42,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_16x_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_16x_k32_merge_update,
	.finup			= ghash_16x_k32_merge_finup,
	.setkey			= ghash_16x_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-input",
	.base.cra_priority	= 41,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_input_update,
	.finup			= ghash_4x_k32_input_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-cross",
	.base.cra_priority	= 40,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_cross_update,
	.finup			= ghash_4x_k32_cross_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tight",
	.base.cra_priority	= 39,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tight_update,
	.finup			= ghash_4x_k32_tight_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-tight-early",
	.base.cra_priority	= 38,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_tight_early_update,
	.finup			= ghash_4x_k32_tight_early_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-pipe",
	.base.cra_priority	= 37,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_k32_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_pipe_update,
	.finup			= ghash_4x_k32_pipe_finup,
	.setkey			= ghash_k32_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-sched",
	.base.cra_priority	= 36,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_sched_update,
	.finup			= ghash_k32_sched_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-u2",
	.base.cra_priority	= 35,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_u2_update,
	.finup			= ghash_k32_u2_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-u4",
	.base.cra_priority	= 34,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_u4_update,
	.finup			= ghash_k32_u4_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-u2-load",
	.base.cra_priority	= 33,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_u2_load_update,
	.finup			= ghash_k32_u2_load_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-u4-load",
	.base.cra_priority	= 32,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_u4_load_update,
	.finup			= ghash_k32_u4_load_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-direct",
	.base.cra_priority	= 31,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_direct_update,
	.finup			= ghash_k32_direct_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-direct-u4-load",
	.base.cra_priority	= 30,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_direct_u4_load_update,
	.finup			= ghash_k32_direct_u4_load_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-packed-sched",
	.base.cra_priority	= 29,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_packed_sched_update,
	.finup			= ghash_k32_packed_sched_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-k32-direct-stream",
	.base.cra_priority	= 28,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_k32_direct_stream_update,
	.finup			= ghash_k32_direct_stream_finup,
	.setkey			= ghash_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-compact",
	.base.cra_priority	= 27,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_compact_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_compact_update,
	.finup			= ghash_4x_k32_compact_finup,
	.setkey			= ghash_compact_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-stack288",
	.base.cra_priority	= 26,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_compact_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_stack288_update,
	.finup			= ghash_4x_k32_stack288_finup,
	.setkey			= ghash_compact_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-stack144",
	.base.cra_priority	= 25,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_compact_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_stack144_update,
	.finup			= ghash_4x_k32_stack144_finup,
	.setkey			= ghash_compact_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-regs32",
	.base.cra_priority	= 24,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_compact_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_regs32_update,
	.finup			= ghash_4x_k32_regs32_finup,
	.setkey			= ghash_compact_setkey,
	.export			= ghash_export,
	.import			= ghash_import,
	.descsize		= sizeof(struct ghash_neon_desc_ctx),
	.statesize		= sizeof(struct ghash_desc_ctx),
}, {
	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-neon-4x-k32-regs0",
	.base.cra_priority	= 23,
	.base.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_neon_compact_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_4x_k32_regs0_update,
	.finup			= ghash_4x_k32_regs0_finup,
	.setkey			= ghash_compact_setkey,
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
MODULE_ALIAS_CRYPTO("ghash-neon-orig-pmull12");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-pmull12");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-pmull13");
MODULE_ALIAS_CRYPTO("ghash-neon-intrinsics");
MODULE_ALIAS_CRYPTO("ghash-neon-orig-direct");
MODULE_ALIAS_CRYPTO("ghash-neon-schoolbook");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-schoolbook");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-schoolbook-reuse");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-schoolbook-load");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-load");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-sched");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tbl");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tbl-simd");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tbl-simd-pipe");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tbl-simd-ldr");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-tbl");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-merge");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-merge-simd");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-merge-ldr");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-merge-early");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-merge-lanes");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-early");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-lanes");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-stp");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-stp-pipe");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-stp");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-stp-pipe");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-gpr-reduce");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-stage");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k16");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k16-load");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k8");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k8-load");
MODULE_ALIAS_CRYPTO("ghash-neon-k32");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-packed");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-no128");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-no64");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-no128-no64");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-no64-early");
MODULE_ALIAS_CRYPTO("ghash-neon-8x-k32-no128-no64-early");
MODULE_ALIAS_CRYPTO("ghash-neon-16x-k32");
MODULE_ALIAS_CRYPTO("ghash-neon-16x-k32-merge");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-input");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-cross");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tight");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-tight-early");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-pipe");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-sched");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-u2");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-u4");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-u2-load");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-u4-load");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-direct");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-direct-u4-load");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-packed-sched");
MODULE_ALIAS_CRYPTO("ghash-neon-k32-direct-stream");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-compact");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-stack288");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-stack144");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-regs32");
MODULE_ALIAS_CRYPTO("ghash-neon-4x-k32-regs0");

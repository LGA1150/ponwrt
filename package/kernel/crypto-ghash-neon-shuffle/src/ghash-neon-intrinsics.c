// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEON intrinsics translation of the single-block byte-PMULL GHASH loop.
 * Derived from lib/crypto/arm64/ghash-neon-core.S.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/neon-intrinsics.h>

#include "ghash-neon-intrinsics.h"

struct ghash_p8_key {
	uint8x16_t h, r1, r2, r3, r4;
};

static __always_inline uint8x16_t rotate64(uint8x16_t a, const int n)
{
	uint8x8_t lo = vget_low_u8(a);
	uint8x8_t hi = vget_high_u8(a);

	return vcombine_u8(vext_u8(lo, lo, n), vext_u8(hi, hi, n));
}

static __always_inline uint64x2_t product(uint8x16_t a, uint8x16_t b,
					  const int high)
{
	poly8x16_t pa = vreinterpretq_p8_u8(a);
	poly8x16_t pb = vreinterpretq_p8_u8(b);

	if (high)
		return vreinterpretq_u64_p16(vmull_high_p8(pa, pb));
	return vreinterpretq_u64_p16(vmull_p8(vget_low_p8(pa), vget_low_p8(pb)));
}

static __always_inline uint8x16_t rotate_product(uint64x2_t a, const int n)
{
	uint8x16_t bytes = vreinterpretq_u8_u64(a);

	return vextq_u8(bytes, bytes, n);
}

static __always_inline uint8x16_t multiply(uint8x16_t a,
					   const struct ghash_p8_key *key,
					   const int high)
{
	const u8 perm1[16] = { 1, 2, 3, 4, 5, 6, 7, 0,
				  9, 10, 11, 12, 13, 14, 15, 8 };
	const u8 perm2[16] = { 2, 3, 4, 5, 6, 7, 0, 1,
				  10, 11, 12, 13, 14, 15, 8, 9 };
	const u8 perm3[16] = { 3, 4, 5, 6, 7, 0, 1, 2,
				  11, 12, 13, 14, 15, 8, 9, 10 };
	const uint64x2_t mask32 = vcombine_u64(vdup_n_u64(0x0000ffffffffffff),
					     vdup_n_u64(0x00000000ffffffff));
	const uint64x2_t mask16 = vcombine_u64(vdup_n_u64(0xffff), vdup_n_u64(0));
	uint8x16_t a1, a2, a3;
	uint64x2_t d, l, m, n, k, t3, t4, t5, t6, t7, t9;

	if (high) {
		a1 = vqtbl1q_u8(a, vld1q_u8(perm1));
		a2 = vqtbl1q_u8(a, vld1q_u8(perm2));
		a3 = vqtbl1q_u8(a, vld1q_u8(perm3));
	} else {
		a1 = rotate64(a, 1);
		a2 = rotate64(a, 2);
		a3 = rotate64(a, 3);
	}
	d = product(a, key->h, high);
	l = veorq_u64(product(a1, key->h, high), product(a, key->r1, high));
	m = veorq_u64(product(a2, key->h, high), product(a, key->r2, high));
	n = veorq_u64(product(a3, key->h, high), product(a, key->r3, high));
	k = product(a, key->r4, high);

	t4 = vuzp1q_u64(l, m);
	t3 = vuzp2q_u64(l, m);
	t6 = vuzp1q_u64(n, k);
	t7 = vuzp2q_u64(n, k);
	t4 = veorq_u64(t4, t3);
	t3 = vandq_u64(t3, mask32);
	t6 = veorq_u64(t6, t7);
	t7 = vandq_u64(t7, mask16);
	t4 = veorq_u64(t4, t3);
	t6 = veorq_u64(t6, t7);
	t5 = vzip2q_u64(t4, t3);
	t3 = vzip1q_u64(t4, t3);
	t9 = vzip2q_u64(t6, t7);
	t7 = vzip1q_u64(t6, t7);

	return veorq_u8(vreinterpretq_u8_u64(d),
		veorq_u8(veorq_u8(rotate_product(t3, 15), rotate_product(t5, 14)),
			 veorq_u8(rotate_product(t7, 13), rotate_product(t9, 12))));
}

static __always_inline uint8x16_t reduce(uint8x16_t lo, uint8x16_t hi,
					 uint8x16_t constant, uint8x16_t last)
{
	uint8x16_t p0 = vreinterpretq_u8_u64(product(lo, constant, 0));
	uint8x16_t p1 = vreinterpretq_u8_u64(product(lo, constant, 1));
	uint8x16_t a = vuzp1q_u8(p0, p1);
	uint8x16_t b = vuzp2q_u8(p0, p1);
	uint8x16_t correction;

	a = vextq_u8(a, a, 1);
	correction = vreinterpretq_u8_u64(product(a, last, 1));
	return veorq_u8(veorq_u8(hi, lo),
			veorq_u8(veorq_u8(b, a), correction));
}

void ghash_neon_intrinsics_blocks(unsigned int blocks, u64 digest[],
				  const u8 *src, const u64 h[])
{
	uint8x16_t hash = vreinterpretq_u8_u64(vld1q_u64((const uint64_t *)h));
	uint8x16_t acc = vreinterpretq_u8_u64(vld1q_u64((const uint64_t *)digest));
	uint8x16_t cross = veorq_u8(hash, vextq_u8(hash, hash, 8));
	const uint8x16_t constant = vdupq_n_u8(0xc2);
	const uint8x16_t last = vsetq_lane_u8(0xc2, vdupq_n_u8(0), 15);
	const struct ghash_p8_key key = {
		.h = hash,
		.r1 = rotate64(hash, 1),
		.r2 = rotate64(hash, 2),
		.r3 = rotate64(hash, 3),
		.r4 = rotate64(hash, 4),
	};
	const struct ghash_p8_key cross_key = {
		.h = cross,
		.r1 = rotate64(cross, 1),
		.r2 = rotate64(cross, 2),
		.r3 = rotate64(cross, 3),
		.r4 = rotate64(cross, 4),
	};

	do {
		uint8x16_t input, mixed, lo, hi, mid, join;

		input = vreinterpretq_u8_u64(vld1q_u64((const uint64_t *)src));
#ifndef CONFIG_CPU_BIG_ENDIAN
		input = vrev64q_u8(input);
#endif
		mixed = veorq_u8(input, vextq_u8(acc, acc, 8));
		acc = veorq_u8(acc, vextq_u8(input, input, 8));
		hi = multiply(acc, &key, 1);
		mixed = veorq_u8(mixed, acc);
		lo = multiply(acc, &key, 0);
		mid = multiply(mixed, &cross_key, 0);
		join = vextq_u8(lo, hi, 8);
		mid = veorq_u8(mid, veorq_u8(lo, hi));
		mid = veorq_u8(mid, join);
		lo = vreinterpretq_u8_u64(vzip1q_u64(vreinterpretq_u64_u8(lo),
						     vreinterpretq_u64_u8(mid)));
		hi = vreinterpretq_u8_u64(vzip2q_u64(vreinterpretq_u64_u8(mid),
						     vreinterpretq_u64_u8(hi)));
		acc = reduce(lo, hi, constant, last);
		src += 16;
	} while (--blocks);

	vst1q_u64((uint64_t *)digest, vreinterpretq_u64_u8(acc));
}

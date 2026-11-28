// SPDX-License-Identifier: GPL-2.0-only
/* Key layouts match the shared comparison assembly, not a public ABI. */
#include "ghash_user.h"
#include <string.h>

_Static_assert(sizeof(struct key_compact) == 80, "compact four-block context");
_Static_assert(offsetof(struct key_compact, h) == 16, "compact reflected powers");
_Static_assert(sizeof(struct key_k32) == 688, "32-bit Karatsuba context");
_Static_assert(offsetof(struct key_k32_8, powers) == 688, "eight-block K32 powers");
_Static_assert(sizeof(struct key_k32_8) == 1120, "eight-block K32 context");
_Static_assert(offsetof(struct key_k32_16, powers) == 1120, "sixteen-block K32 powers");
_Static_assert(sizeof(struct key_k32_16) == 1984, "sixteen-block K32 context");
_Static_assert(sizeof(struct key_deep) == 688, "deep Karatsuba context");
_Static_assert(sizeof(struct key2) == 256, "pair context");
_Static_assert(offsetof(struct key4, powers) == 256, "four-block powers");
_Static_assert(offsetof(struct key8, powers) == 736, "eight-block powers");
_Static_assert(sizeof(struct key8) == 1696, "eight-block context");
_Static_assert(offsetof(struct key_school4, powers) == 16, "schoolbook powers");
_Static_assert(sizeof(struct key_school4) == 592, "schoolbook context");
_Static_assert(offsetof(struct key_umull, cross) == 144, "integer products");

#define DECL(tag) \
	extern void ghash_neon_##tag##_blocks(unsigned int, uint64_t *, \
					    const uint8_t *, const uint64_t *)
DECL(shuffle); DECL(sched); DECL(gpr); DECL(2x); DECL(2x_pmull);
DECL(pmull); DECL(orig_pmull); DECL(fused); DECL(2x_reuse); DECL(2x_load);
DECL(4x); DECL(8x); DECL(orig_pmull13); DECL(pmul); DECL(umull);
DECL(orig_pmull12); DECL(4x_pmull12); DECL(4x_pmull13); DECL(intrinsics);
DECL(orig_direct); DECL(schoolbook); DECL(4x_schoolbook);
DECL(4x_schoolbook_reuse);
DECL(4x_schoolbook_load);
DECL(8x_k32_tbl);
DECL(4x_k32_stp); DECL(4x_k32_stp_pipe);
DECL(8x_k32_stp); DECL(8x_k32_stp_pipe);
DECL(8x_k32_merge); DECL(8x_k32_merge_simd); DECL(8x_k32_merge_ldr);
DECL(8x_k32_merge_early); DECL(8x_k32_early);
DECL(8x_k32_merge_lanes); DECL(8x_k32_lanes);
DECL(4x_k32_input); DECL(4x_k32_cross); DECL(4x_k32_tight); DECL(4x_k32_tight_early);
DECL(4x_k32_stack288);
DECL(4x_k32_stack144);
DECL(4x_k32_regs32);
DECL(4x_k32_regs0);
DECL(4x_k32_pipe); DECL(4x_k32_compact);
DECL(16x_k32); DECL(16x_k32_merge);
DECL(8x_k32_no128); DECL(8x_k32_no64); DECL(8x_k32_no128_no64);
DECL(8x_k32_no64_early); DECL(8x_k32_no128_no64_early);
DECL(4x_k32); DECL(4x_k32_load); DECL(4x_k32_sched); DECL(4x_k32_tbl);
DECL(4x_k32_tbl_simd); DECL(4x_k32_tbl_simd_pipe); DECL(4x_k32_tbl_simd_ldr);
DECL(4x_gpr_reduce); DECL(4x_stage);
DECL(k32); DECL(k32_packed); DECL(k32_direct_stream);
DECL(k32_direct); DECL(k32_direct_u4_load); DECL(k32_packed_sched);
DECL(k32_sched); DECL(k32_u2); DECL(k32_u4); DECL(k32_u2_load); DECL(k32_u4_load);
DECL(4x_k16); DECL(4x_k16_load); DECL(4x_k8); DECL(4x_k8_load);

#define VAR(name, tag, kind) { "ghash-neon-" name, ghash_neon_##tag##_blocks, kind }
const struct variant variants[] = {
	VAR("shuffle", shuffle, SINGLE),
	VAR("sched", sched, SINGLE),
	VAR("gpr", gpr, SINGLE),
	VAR("2x", 2x, PAIR),
	VAR("2x-pmull", 2x_pmull, PAIR),
	VAR("pmull", pmull, SINGLE),
	VAR("orig-pmull", orig_pmull, SINGLE),
	VAR("fused", fused, FUSED),
	VAR("2x-reuse", 2x_reuse, PAIR),
	VAR("2x-load", 2x_load, PAIR),
	VAR("4x", 4x, FOUR),
	VAR("8x", 8x, EIGHT),
	VAR("orig-pmull13", orig_pmull13, SINGLE),
	VAR("pmul", pmul, SINGLE),
	VAR("umull", umull, UMULL),
	VAR("orig-pmull12", orig_pmull12, SINGLE),
	VAR("4x-pmull12", 4x_pmull12, FOUR),
	VAR("4x-pmull13", 4x_pmull13, FOUR),
	VAR("intrinsics", intrinsics, SINGLE),
	VAR("orig-direct", orig_direct, SINGLE),
	VAR("schoolbook", schoolbook, SINGLE),
	VAR("4x-schoolbook", 4x_schoolbook, SCHOOL4),
	VAR("4x-schoolbook-reuse", 4x_schoolbook_reuse, SCHOOL4),
	VAR("4x-schoolbook-load", 4x_schoolbook_load, SCHOOL4),
	VAR("4x-k32-stack288", 4x_k32_stack288, K32_COMPACT),
	VAR("4x-k32-stack144", 4x_k32_stack144, K32_COMPACT),
	VAR("4x-k32-regs32", 4x_k32_regs32, K32_COMPACT),
	VAR("4x-k32-regs0", 4x_k32_regs0, K32_COMPACT),
	VAR("4x-k32-compact", 4x_k32_compact, K32_COMPACT),
	VAR("4x-k32", 4x_k32, K32),
	VAR("4x-k32-load", 4x_k32_load, K32),
	VAR("4x-k32-sched", 4x_k32_sched, K32),
	VAR("4x-k32-tbl", 4x_k32_tbl, K32),
	VAR("4x-gpr-reduce", 4x_gpr_reduce, FOUR),
	VAR("4x-stage", 4x_stage, FOUR),
	VAR("4x-k16", 4x_k16, K16),
	VAR("4x-k16-load", 4x_k16_load, K16),
	VAR("4x-k8", 4x_k8, K8),
	VAR("4x-k8-load", 4x_k8_load, K8),
	VAR("k32", k32, SINGLE),
	VAR("k32-packed", k32_packed, SINGLE),
	VAR("k32-direct-stream", k32_direct_stream, SINGLE),
	VAR("k32-direct", k32_direct, SINGLE),
	VAR("k32-direct-u4-load", k32_direct_u4_load, SINGLE),
	VAR("k32-packed-sched", k32_packed_sched, SINGLE),
	VAR("k32-sched", k32_sched, SINGLE),
	VAR("k32-u2", k32_u2, SINGLE),
	VAR("k32-u4", k32_u4, SINGLE),
	VAR("k32-u2-load", k32_u2_load, SINGLE),
	VAR("k32-u4-load", k32_u4_load, SINGLE),
	VAR("4x-k32-tbl-simd", 4x_k32_tbl_simd, K32),
	VAR("4x-k32-tbl-simd-pipe", 4x_k32_tbl_simd_pipe, K32),
	VAR("4x-k32-tbl-simd-ldr", 4x_k32_tbl_simd_ldr, K32),
	VAR("8x-k32-tbl", 8x_k32_tbl, K32_EIGHT),
	VAR("4x-k32-stp", 4x_k32_stp, K32),
	VAR("4x-k32-stp-pipe", 4x_k32_stp_pipe, K32),
	VAR("8x-k32-stp", 8x_k32_stp, K32_EIGHT),
	VAR("8x-k32-stp-pipe", 8x_k32_stp_pipe, K32_EIGHT),
	VAR("8x-k32-merge", 8x_k32_merge, K32_EIGHT),
	VAR("8x-k32-merge-simd", 8x_k32_merge_simd, K32_EIGHT),
	VAR("8x-k32-merge-ldr", 8x_k32_merge_ldr, K32_EIGHT),
	VAR("8x-k32-merge-early", 8x_k32_merge_early, K32_EIGHT),
	VAR("8x-k32-early", 8x_k32_early, K32_EIGHT),
	VAR("8x-k32-merge-lanes", 8x_k32_merge_lanes, K32_EIGHT),
	VAR("8x-k32-lanes", 8x_k32_lanes, K32_EIGHT),
	VAR("4x-k32-input", 4x_k32_input, K32),
	VAR("4x-k32-cross", 4x_k32_cross, K32),
	VAR("4x-k32-tight", 4x_k32_tight, K32),
	VAR("4x-k32-tight-early", 4x_k32_tight_early, K32),
	VAR("4x-k32-pipe", 4x_k32_pipe, K32),
	VAR("16x-k32", 16x_k32, K32_SIXTEEN),
	VAR("16x-k32-merge", 16x_k32_merge, K32_SIXTEEN),
	VAR("8x-k32-no128", 8x_k32_no128, K32_EIGHT),
	VAR("8x-k32-no64", 8x_k32_no64, K32_EIGHT),
	VAR("8x-k32-no128-no64", 8x_k32_no128_no64, K32_EIGHT),
	VAR("8x-k32-no64-early", 8x_k32_no64_early, K32_EIGHT),
	VAR("8x-k32-no128-no64-early", 8x_k32_no128_no64_early, K32_EIGHT),
};
const size_t nr_variants = sizeof(variants) / sizeof(variants[0]);

static uint64_t load_be64(const uint8_t *p)
{
	uint64_t v = 0;

	for (unsigned int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

void ghash_export(const uint64_t state[2], uint8_t out[16])
{
	for (unsigned int i = 0; i < 8; i++) {
		out[i] = state[1] >> (56 - i * 8);
		out[i + 8] = state[0] >> (56 - i * 8);
	}
}

/* Independent bit-serial GHASH field arithmetic in network byte order. */
static void field_mul(uint8_t out[16], const uint8_t x[16], const uint8_t h[16])
{
	uint8_t z[16] = {0}, v[16];

	memcpy(v, h, sizeof(v));
	for (unsigned int i = 0; i < 128; i++) {
		uint8_t mask = -(uint8_t)((x[i / 8] >> (7 - i % 8)) & 1);
		uint8_t carry = v[15] & 1;

		for (unsigned int j = 0; j < 16; j++)
			z[j] ^= v[j] & mask;
		for (unsigned int j = 15; j; j--)
			v[j] = (v[j] >> 1) | (v[j - 1] << 7);
		v[0] = (v[0] >> 1) ^ (0xe1 & -(uint8_t)carry);
	}
	memcpy(out, z, sizeof(z));
}

void ghash_reference(const uint8_t h[16], const uint8_t *src, size_t len,
		     uint8_t out[16])
{
	uint8_t acc[16] = {0};

	while (len) {
		size_t take = len < 16 ? len : 16;

		for (size_t i = 0; i < take; i++)
			acc[i] ^= src[i];
		field_mul(acc, acc, h);
		src += take;
		len -= take;
	}
	memcpy(out, acc, 16);
}

static void reflect(uint64_t out[2], const uint8_t h[16])
{
	uint64_t a = load_be64(h), b = load_be64(h + 8), carry = a >> 63;

	out[0] = (b << 1) | carry;
	out[1] = (a << 1) | (b >> 63);
	if (carry)
		out[1] ^= UINT64_C(0xc200000000000000);
}

static uint64_t rotate(uint64_t v, unsigned int bytes)
{
	unsigned int n = bytes * 8;

	return n ? (v >> n) | (v << (64 - n)) : v;
}

static void pair_table(uint64_t table[3][5][2], const uint64_t a[2],
		       const uint64_t b[2])
{
	for (unsigned int part = 0; part < 3; part++) {
		uint64_t x = part < 2 ? a[part] : a[0] ^ a[1];
		uint64_t y = part < 2 ? b[part] : b[0] ^ b[1];

		for (unsigned int rot = 0; rot < 5; rot++) {
			table[part][rot][0] = rotate(x, rot);
			table[part][rot][1] = rotate(y, rot);
		}
	}
}

static void k32_table(uint64_t table[3][3][3][2], const uint64_t p[4][2])
{
	for (unsigned int part = 0; part < 3; part++) {
		uint64_t a[4];

		for (unsigned int i = 0; i < 4; i++)
			a[i] = part < 2 ? p[3 - i][part] :
				p[3 - i][0] ^ p[3 - i][1];
		for (unsigned int group = 0; group < 3; group++)
			for (unsigned int r = 0; r < 3; r++)
				for (unsigned int lane = 0; lane < 4; lane++) {
					uint32_t v = group < 2 ?
						a[group * 2 + lane / 2] >>
						((lane % 2) * 32) :
						(uint32_t)a[lane] ^
						(uint32_t)(a[lane] >> 32);

					if (r)
						v = (v >> (8 * r)) | (v << (32 - 8 * r));
					table[part][group][r][lane / 2] |=
						(uint64_t)v << ((lane % 2) * 32);
				}
	}
}

void ghash_key_init(union ghash_key *key, enum key_kind kind,
		    const uint8_t h[16])
{
	uint8_t power[16];
	uint64_t p[16][2], pairs[3][5][2];
	unsigned int deep = kind == K16 || kind == K8;
	unsigned int count = kind == K32_SIXTEEN ? 16 :
		(kind == EIGHT || kind == K32_EIGHT) ? 8 :
		(kind == FOUR || kind == SCHOOL4 || kind == K32 ||
		 kind == K32_COMPACT || deep) ? 4 :
		kind == PAIR ? 2 : 1;

	memset(key, 0, sizeof(*key));
	memcpy(power, h, 16);
	for (unsigned int i = 0; i < count; i++) {
		if (i)
			field_mul(power, power, h);
		reflect(p[i], power);
	}
	if (kind == K32_COMPACT) {
		memcpy(key->compact.raw, h, 16);
		memcpy(key->compact.h, p, sizeof(key->compact.h));
		return;
	}
	memcpy(key->single, p[0], 16);
	if (kind == PAIR || kind == FOUR || kind == EIGHT || kind == K32 ||
	    kind == K32_EIGHT || kind == K32_SIXTEEN || deep) {
		struct key2 *pair = kind == PAIR ? &key->pair :
			kind == FOUR ? &key->four.tail : kind == K32 ? &key->k32.tail :
			kind == K32_EIGHT ? &key->k32_8.tail.tail :
			kind == K32_SIXTEEN ? &key->k32_16.tail.tail.tail :
			deep ? &key->deep.tail :
			&key->eight.tail.tail;

		pair_table(pair->powers, p[1], p[0]);
	}
	if (kind == K32 || kind == K32_EIGHT || kind == K32_SIXTEEN) {
		struct key_k32_8 *eight = kind == K32_SIXTEEN ?
			&key->k32_16.tail : &key->k32_8;
		struct key_k32 *k32 = kind == K32 ? &key->k32 : &eight->tail;

		k32_table(k32->powers, p);
		if (kind != K32)
			k32_table(eight->powers, p + 4);
		if (kind == K32_SIXTEEN) {
			k32_table(key->k32_16.powers[0], p + 8);
			k32_table(key->k32_16.powers[1], p + 12);
		}
	}
	if (deep) {
		for (unsigned int part = 0; part < 3; part++) {
			uint64_t a[4];

			for (unsigned int i = 0; i < 4; i++)
				a[i] = part < 2 ? p[3 - i][part] :
					p[3 - i][0] ^ p[3 - i][1];
			for (unsigned int group = 0; group < 3; group++) {
				uint64_t (*table)[2] = key->deep.operands[part][group];

				for (unsigned int lane = 0; lane < 4; lane++) {
					uint32_t v = group < 2 ?
						a[group * 2 + lane / 2] >> ((lane % 2) * 32) :
						(uint32_t)a[lane] ^ (uint32_t)(a[lane] >> 32);
					uint16_t cross = v ^ (v >> 16);

					table[0][lane / 2] |= (uint64_t)v << ((lane % 2) * 32);
					table[2][0] |= (uint64_t)cross << (lane * 16);
					if (kind == K16) {
						uint32_t swap = ((v & 0x00ff00ffU) << 8) |
							((v >> 8) & 0x00ff00ffU);
						uint16_t swap_cross = (cross >> 8) | (cross << 8);

						table[1][lane / 2] |= (uint64_t)swap << ((lane % 2) * 32);
						table[2][1] |= (uint64_t)swap_cross << (lane * 16);
					} else {
						uint32_t halves = v ^ (v >> 8);
						uint8_t all = cross ^ (cross >> 8);

						table[1][0] |= (uint64_t)(halves & 0xff) << (lane * 16);
						table[1][0] |= (uint64_t)((halves >> 16) & 0xff) << (lane * 16 + 8);
						table[2][1] |= (uint64_t)all << (lane * 8);
						table[2][1] |= (uint64_t)all << (lane * 8 + 32);
					}
				}
			}
		}
	}
	if (kind == FOUR || kind == EIGHT) {
		struct key4 *four = kind == FOUR ? &key->four : &key->eight.tail;

		pair_table(pairs, p[3], p[2]);
		for (unsigned int part = 0; part < 3; part++) {
			memcpy(four->powers[part][0], pairs[part], sizeof(pairs[part]));
			memcpy(four->powers[part][1], four->tail.powers[part],
			       sizeof(pairs[part]));
		}
	}
	if (kind == EIGHT) {
		for (unsigned int pair = 0; pair < 4; pair++) {
			pair_table(pairs, p[7 - 2 * pair], p[6 - 2 * pair]);
			for (unsigned int part = 0; part < 3; part++)
				memcpy(key->eight.powers[part][pair], pairs[part],
				       sizeof(pairs[part]));
		}
	} else if (kind == SCHOOL4) {
		for (unsigned int i = 0; i < 4; i++) {
			uint64_t a = p[3 - i][0], b = p[3 - i][1];

			key->school4.powers[0][i][0] = a;
			key->school4.powers[0][i][1] = b;
			for (unsigned int r = 1; r < 8; r++) {
				key->school4.powers[r][i][0] = (a >> (r * 8)) | (b << (64 - r * 8));
				key->school4.powers[r][i][1] = (b >> (r * 8)) | (a << (64 - r * 8));
			}
			key->school4.powers[8][i][0] = b;
			key->school4.powers[8][i][1] = a;
		}
	} else if (kind == FUSED) {
		uint64_t a = p[0][0], k[2];

		k[0] = (a << 57) ^ (a << 62) ^ (a << 63) ^ p[0][1];
		k[1] = (a >> 7) ^ (a >> 2) ^ (a >> 1) ^ a;
		for (unsigned int part = 0; part < 2; part++)
			for (unsigned int r = 0; r < 5; r++) {
				key->fused.pairs[part][r][0] = rotate(k[part], r);
				key->fused.pairs[part][r][1] = rotate(p[0][part], r);
			}
	} else if (kind == UMULL) {
		uint32_t w[4], pairs32[5][2];

		for (unsigned int i = 0; i < 4; i++)
			w[i] = p[0][i / 2] >> (32 * (i % 2));
		pairs32[0][0] = w[0]; pairs32[0][1] = w[1];
		pairs32[1][0] = w[2]; pairs32[1][1] = w[3];
		pairs32[2][0] = w[0] ^ w[1]; pairs32[2][1] = w[2] ^ w[3];
		pairs32[3][0] = w[0] ^ w[2]; pairs32[3][1] = w[1] ^ w[3];
		pairs32[4][0] = pairs32[2][0] ^ pairs32[2][1];
		for (unsigned int i = 0; i < 4; i++)
			for (unsigned int j = 0; j < 4; j++)
				for (unsigned int lane = 0; lane < 2; lane++)
					key->umull.pairs[i][j][lane] = pairs32[i][lane] & (0x11111111U << j);
		for (unsigned int j = 0; j < 4; j++)
			key->umull.cross[j] = pairs32[4][0] & (0x11111111U << ((-j) & 3));
	}
}

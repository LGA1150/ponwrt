/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GHASH_USER_H
#define GHASH_USER_H

#include <stddef.h>
#include <stdint.h>

typedef void (*ghash_fn)(unsigned int, uint64_t *, const uint8_t *,
			 const uint64_t *);

struct key2 {
	uint64_t single[2];
	uint64_t powers[3][5][2];
};

struct key4 {
	struct key2 tail;
	uint64_t powers[3][2][5][2];
};

struct key8 {
	struct key4 tail;
	uint64_t powers[3][4][5][2];
};

struct key_compact {
	uint8_t raw[16];
	uint64_t h[4][2];
};

struct key_k32 {
	struct key2 tail;
	uint64_t powers[3][3][3][2];
};

struct key_k32_8 {
	struct key_k32 tail;
	uint64_t powers[3][3][3][2];
};

struct key_k32_16 {
	struct key_k32_8 tail;
	uint64_t powers[2][3][3][3][2];
};

struct key_deep {
	struct key2 tail;
	uint64_t operands[3][3][3][2];
};

struct key_school4 {
	uint64_t single[2];
	uint64_t powers[9][4][2];
};

struct key_fused {
	uint64_t single[2];
	uint64_t pairs[2][5][2];
};

struct key_umull {
	uint64_t single[2];
	uint32_t pairs[4][4][2];
	uint32_t cross[4];
};

union ghash_key {
	uint64_t single[2];
	struct key2 pair;
	struct key4 four;
	struct key8 eight;
	struct key_k32 k32;
	struct key_compact compact;
	struct key_k32_8 k32_8;
	struct key_k32_16 k32_16;
	struct key_deep deep;
	struct key_school4 school4;
	struct key_fused fused;
	struct key_umull umull;
};

enum key_kind {
	SINGLE, PAIR, FOUR, EIGHT, SCHOOL4, FUSED, UMULL, K32, K16, K8,
	K32_EIGHT, K32_SIXTEEN, K32_COMPACT
};

struct variant {
	const char *name;
	ghash_fn blocks;
	enum key_kind kind;
};

extern const struct variant variants[];
extern const size_t nr_variants;

void ghash_key_init(union ghash_key *key, enum key_kind kind,
		    const uint8_t h[16]);
void ghash_reference(const uint8_t h[16], const uint8_t *src, size_t len,
		     uint8_t out[16]);
void ghash_export(const uint64_t state[2], uint8_t out[16]);

/* The raw kernel functions do not preserve AAPCS64's d8-d15. */
void ghash_user_blocks(ghash_fn fn, unsigned int blocks, uint64_t state[2],
		       const uint8_t *src, const union ghash_key *key);
void ghash_user_repeat(ghash_fn fn, unsigned int blocks, uint64_t state[2],
		       const uint8_t *src, const union ghash_key *key,
		       uint64_t repeats);
unsigned int ghash_user_abi_check(ghash_fn fn, unsigned int blocks,
				 uint64_t state[2], const uint8_t *src,
				 const union ghash_key *key);

#endif

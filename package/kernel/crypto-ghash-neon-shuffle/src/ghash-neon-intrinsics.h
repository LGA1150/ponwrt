/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GHASH_NEON_INTRINSICS_H
#define _GHASH_NEON_INTRINSICS_H

#include <asm-generic/int-ll64.h>

void ghash_neon_intrinsics_blocks(unsigned int blocks, u64 digest[],
				  const u8 *src, const u64 h[]);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GHASH_USER_ASSEMBLER_H
#define GHASH_USER_ASSEMBLER_H
#ifdef __AARCH64EB__
#define CPU_LE(...)
#define CPU_BE(...) __VA_ARGS__
#else
#define CPU_LE(...) __VA_ARGS__
#define CPU_BE(...)
#endif
	.macro adr_l, dst, symbol
	adrp	\dst, \symbol
	add	\dst, \dst, :lo12:\symbol
	.endm

	.macro mov_q, dst, value
	movz	\dst, :abs_g3:\value
	movk	\dst, :abs_g2_nc:\value
	movk	\dst, :abs_g1_nc:\value
	movk	\dst, :abs_g0_nc:\value
	.endm
#endif

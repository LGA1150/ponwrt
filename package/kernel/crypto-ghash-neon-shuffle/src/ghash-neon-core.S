/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Standalone GHASH using ARM64 ASIMD byte polynomial multiplication.
 * Derived from arch/arm64/crypto/ghash-ce-core.S in Linux 6.18.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH		.req	v0
	SHASH2		.req	v1
	T1		.req	v2
	T2		.req	v3
	NEXT		.req	v4
	XM		.req	v5
	XL		.req	v6
	XH		.req	v7
	IN1		.req	v7

	k00_16		.req	v8
	k32_48		.req	v9

	t3		.req	v10
	t4		.req	v11
	t5		.req	v12
	t6		.req	v13
	t7		.req	v14
	t8		.req	v15
	t9		.req	v16

	perm1		.req	v17
	perm2		.req	v18
	perm3		.req	v19

	sh1		.req	v20
	sh2		.req	v21
	sh3		.req	v22
	sh4		.req	v23

	ss1		.req	v24
	ss2		.req	v25
	ss3		.req	v26
	ss4		.req	v27

	pack1		.req	v28
	pack2		.req	v29
	pack3		.req	v30
	pack4		.req	v31
	REDUCE_CONST	.req	v28	// free in the original, unfused loop
	REDUCE_LAST	.req	v29
	REDUCE_PERM	.req	v30

	.text
	.arch		armv8-a

	.macro		__pmull_p8, rq, ad, bd, scheduled=0, orig=0
	ext		t3.8b, \ad\().8b, \ad\().8b, #1		// A1
	ext		t5.8b, \ad\().8b, \ad\().8b, #2		// A2
	ext		t7.8b, \ad\().8b, \ad\().8b, #3		// A3

	__pmull_p8_\bd	\rq, \ad, \scheduled, \orig
	.endm

	.macro		__pmull2_p8, rq, ad, bd, scheduled=0, orig=0
	tbl		t3.16b, {\ad\().16b}, perm1.16b		// A1
	tbl		t5.16b, {\ad\().16b}, perm2.16b		// A2
	tbl		t7.16b, {\ad\().16b}, perm3.16b		// A3

	__pmull2_p8_\bd	\rq, \ad, \scheduled, \orig
	.endm

	.macro		__pmull_p8_SHASH, rq, ad, scheduled, orig
	__pmull_p8_tail	\rq, \ad\().8b, SHASH.8b, 8b,, sh1, sh2, sh3, sh4, \scheduled, \orig
	.endm

	.macro		__pmull_p8_SHASH2, rq, ad, scheduled, orig
	__pmull_p8_tail	\rq, \ad\().8b, SHASH2.8b, 8b,, ss1, ss2, ss3, ss4, \scheduled, \orig
	.endm

	.macro		__pmull2_p8_SHASH, rq, ad, scheduled, orig
	__pmull_p8_tail	\rq, \ad\().16b, SHASH.16b, 16b, 2, sh1, sh2, sh3, sh4, \scheduled, \orig
	.endm

	.macro		__pmull_p8_tail, rq, ad, bd, nb, t, b1, b2, b3, b4, scheduled, orig
	.if		\scheduled
	/* Start with an unrotated operand, then fill XOR latency with multiplies. */
	pmull\t		t4.8h, \ad, \b1\().\nb			// E = A*B1
	pmull\t		t3.8h, t3.\nb, \bd			// F = A1*B
	pmull\t		t6.8h, \ad, \b2\().\nb			// G = A*B2
	pmull\t		t5.8h, t5.\nb, \bd			// H = A2*B
	pmull\t		t8.8h, \ad, \b3\().\nb			// I = A*B3
	pmull\t		t7.8h, t7.\nb, \bd			// J = A3*B
	eor		t3.16b, t3.16b, t4.16b			// L = E + F
	eor		t5.16b, t5.16b, t6.16b			// M = G + H
	pmull\t		t9.8h, \ad, \b4\().\nb			// K = A*B4
	eor		t7.16b, t7.16b, t8.16b			// N = I + J
	/* rq may alias ad: this must remain the last multiply reading ad. */
	pmull\t		\rq\().8h, \ad, \bd			// D = A*B
	.else
	pmull\t		t3.8h, t3.\nb, \bd			// F = A1*B
	pmull\t		t4.8h, \ad, \b1\().\nb			// E = A*B1
	pmull\t		t5.8h, t5.\nb, \bd			// H = A2*B
	pmull\t		t6.8h, \ad, \b2\().\nb			// G = A*B2
	pmull\t		t7.8h, t7.\nb, \bd			// J = A3*B
	pmull\t		t8.8h, \ad, \b3\().\nb			// I = A*B3
	pmull\t		t9.8h, \ad, \b4\().\nb			// K = A*B4
	pmull\t		\rq\().8h, \ad, \bd			// D = A*B

	eor		t3.16b, t3.16b, t4.16b			// L = E + F
	eor		t5.16b, t5.16b, t6.16b			// M = G + H
	eor		t7.16b, t7.16b, t8.16b			// N = I + J
	.endif

	__pmull_p8_combine \rq, \orig
	.endm

	.macro		__pmull_p8_combine, rq, orig=0
	uzp1		t4.2d, t3.2d, t5.2d
	uzp2		t3.2d, t3.2d, t5.2d
	uzp1		t6.2d, t7.2d, t9.2d
	uzp2		t7.2d, t7.2d, t9.2d

	// t3 = (L) (P0 + P1) << 8
	// t5 = (M) (P2 + P3) << 16
	eor		t4.16b, t4.16b, t3.16b
	and		t3.16b, t3.16b, k32_48.16b

	// t7 = (N) (P4 + P5) << 24
	// t9 = (K) (P6 + P7) << 32
	eor		t6.16b, t6.16b, t7.16b
	and		t7.16b, t7.16b, k00_16.16b

	eor		t4.16b, t4.16b, t3.16b
	eor		t6.16b, t6.16b, t7.16b

	.if		\orig
	zip2		t5.2d, t4.2d, t3.2d
	zip1		t3.2d, t4.2d, t3.2d
	zip2		t9.2d, t6.2d, t7.2d
	zip1		t7.2d, t6.2d, t7.2d

	ext		t3.16b, t3.16b, t3.16b, #15
	ext		t5.16b, t5.16b, t5.16b, #14
	ext		t7.16b, t7.16b, t7.16b, #13
	ext		t9.16b, t9.16b, t9.16b, #12
	.else
	/*
	 * Fuse each 64-bit ZIP and byte rotation into one fixed permutation.
	 * Read both members of each pair before overwriting a table register.
	 */
	tbl		t5.16b, {t3.16b, t4.16b}, pack2.16b
	tbl		t3.16b, {t3.16b, t4.16b}, pack1.16b
	tbl		t9.16b, {t6.16b, t7.16b}, pack4.16b
	tbl		t7.16b, {t6.16b, t7.16b}, pack3.16b
	.endif

	eor		t3.16b, t3.16b, t5.16b
	eor		t7.16b, t7.16b, t9.16b
	eor		\rq\().16b, \rq\().16b, t3.16b
	eor		\rq\().16b, \rq\().16b, t7.16b
	.endm

	.macro		__pmull_pre_p8, keys=1, orig=0
	.if		!\orig
	adr_l		x5, .Lghash_neon_pack
	ld1		{pack1.16b-pack4.16b}, [x5]
	.endif

	.if		\keys
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b
	.endif

	// k00_16 := 0x0000000000000000_000000000000ffff
	// k32_48 := 0x00000000ffffffff_0000ffffffffffff
	movi		k32_48.2d, #0xffffffff
	mov		k32_48.h[2], k32_48.h[0]
	ushr		k00_16.2d, k32_48.2d, #32

	// prepare the permutation vectors
	mov_q		x5, 0x080f0e0d0c0b0a09
	movi		T1.8b, #8
	dup		perm1.2d, x5
	eor		perm1.16b, perm1.16b, T1.16b
	ushr		perm2.2d, perm1.2d, #8
	ushr		perm3.2d, perm1.2d, #16
	ushr		T1.2d, perm1.2d, #24
	sli		perm2.2d, perm1.2d, #56
	sli		perm3.2d, perm1.2d, #48
	sli		T1.2d, perm1.2d, #40

	// precompute loop invariants
	.if		\keys
	tbl		sh1.16b, {SHASH.16b}, perm1.16b
	tbl		sh2.16b, {SHASH.16b}, perm2.16b
	tbl		sh3.16b, {SHASH.16b}, perm3.16b
	tbl		sh4.16b, {SHASH.16b}, T1.16b
	ext		ss1.8b, SHASH2.8b, SHASH2.8b, #1
	ext		ss2.8b, SHASH2.8b, SHASH2.8b, #2
	ext		ss3.8b, SHASH2.8b, SHASH2.8b, #3
	ext		ss4.8b, SHASH2.8b, SHASH2.8b, #4
	.endif
	.endm

	.macro		__pmull_reduce_p8
	eor		XM.16b, XM.16b, T1.16b

	zip1		XL.2d, XL.2d, XM.2d
	zip2		XH.2d, XM.2d, XH.2d

	shl		T1.2d, XL.2d, #57
	shl		T2.2d, XL.2d, #62
	eor		T2.16b, T2.16b, T1.16b
	shl		T1.2d, XL.2d, #63
	eor		T2.16b, T2.16b, T1.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, T2.16b, T1.16b

	zip1		XL.2d, XL.2d, T2.2d
	zip2		XH.2d, T2.2d, XH.2d

	ushr		T2.2d, XL.2d, #1
	eor		XH.16b, XH.16b, XL.16b
	eor		XL.16b, XL.16b, T2.16b
	ushr		T2.2d, T2.2d, #6
	ushr		XL.2d, XL.2d, #1
	.endm

	.macro		__pmull_reduce_p8_sched, preload_gpr=0
	eor		XM.16b, XM.16b, T1.16b
	zip1		XL.2d, XL.2d, XM.2d
	.if		\preload_gpr
	ldr		x6, [x2]
	.endif
	zip2		XH.2d, XM.2d, XH.2d
	.if		\preload_gpr
	ldr		x7, [x2, #8]
	.endif

	/* Independent shifts feed a balanced XOR tree instead of a chain. */
	shl		T1.2d, XL.2d, #57
	.if		\preload_gpr
CPU_LE(	rev		x6, x6		)
	.endif
	shl		T2.2d, XL.2d, #62
	.if		\preload_gpr
CPU_LE(	rev		x7, x7		)
	.endif
	shl		t8.2d, XL.2d, #63
	.if		\preload_gpr
	add		x2, x2, #16
	.endif
	ext		t9.16b, XL.16b, XH.16b, #8
	eor		T2.16b, T2.16b, T1.16b
	eor		t8.16b, t8.16b, t9.16b
	eor		T2.16b, T2.16b, t8.16b
	zip1		XL.2d, XL.2d, T2.2d
	zip2		XH.2d, T2.2d, XH.2d
	.if		\preload_gpr
	/* FMOV initializes NEXT's low half and clears its high half. */
	fmov		d4, x6
	.endif

	/* (L ^ (L >> 1)) >> 1 == (L >> 1) ^ (L >> 2), per lane. */
	ushr		T1.2d, XL.2d, #1
	ushr		T2.2d, XL.2d, #2
	.if		\preload_gpr
	mov		NEXT.d[1], x7
	.endif
	ushr		t8.2d, XL.2d, #7
	eor		XH.16b, XH.16b, XL.16b
	eor		T1.16b, T1.16b, T2.16b
	eor		t8.16b, t8.16b, XH.16b
	eor		XL.16b, T1.16b, t8.16b
	.endm

	/* blocks is nonzero; callers hold the kernel SIMD context. */
SYM_FUNC_START(ghash_neon_shuffle_blocks)
	ld1		{SHASH.2d}, [x3]
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8

.Lghash_loop:
	ld1		{T1.2d}, [x2], #16
	sub		w0, w0, #1

	/* multiply XL by SHASH in GF(2^128) */
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_p8	XH, XL, SHASH		// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	__pmull_p8	XL, XL, SHASH		// a0 * b0
	__pmull_p8	XM, T1, SHASH2		// (a1 + a0)(b1 + b0)

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b

	__pmull_reduce_p8

	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbnz		w0, .Lghash_loop

	st1		{XL.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_shuffle_blocks)

	/* Same field arithmetic, with independent work moved off critical paths. */
	.macro		__ghash_sched, mul_reduce=0
	ld1		{SHASH.2d}, [x3]
	ld1		{XL.2d}, [x1]
	ld1		{NEXT.2d}, [x2], #16
	__pmull_pre_p8

1:
	ext		T2.16b, XL.16b, XL.16b, #8
CPU_LE(	rev64		T1.16b, NEXT.16b	)
CPU_BE(	mov		T1.16b, NEXT.16b	)
	sub		w0, w0, #1
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_p8	XH, XL, SHASH, 1
	eor		T1.16b, T1.16b, XL.16b
	/* Never load beyond the full blocks supplied by the shash frontend. */
	cbz		w0, 2f
	ld1		{NEXT.2d}, [x2], #16
2:
	__pmull_p8	XL, XL, SHASH, 1
	__pmull_p8	XM, T1, SHASH2, 1

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	.if		\mul_reduce
	/* SHASH2 retains the cross-product key between single blocks. */
	movi		T2.16b, #0xc2
	__pmull_reduce_p8_mul T2
	.else
	__pmull_reduce_p8_sched
	.endif
	cbnz		w0, 1b

	st1		{XL.2d}, [x1]
	ret
	.endm

SYM_FUNC_START(ghash_neon_sched_blocks)
	__ghash_sched
SYM_FUNC_END(ghash_neon_sched_blocks)

	/* Interleave next-block GPR loads with the current block's reduction. */
SYM_FUNC_START(ghash_neon_gpr_blocks)
	ldr		x6, [x2]
	ld1		{SHASH.2d}, [x3]
	ldr		x7, [x2, #8]
	ld1		{XL.2d}, [x1]
CPU_LE(	rev		x6, x6		)
CPU_LE(	rev		x7, x7		)
	add		x2, x2, #16
	__pmull_pre_p8
	fmov		d4, x6
	mov		NEXT.d[1], x7

.Lghash_gpr_loop:
	ext		T2.16b, XL.16b, XL.16b, #8
	sub		w0, w0, #1
	ext		IN1.16b, NEXT.16b, NEXT.16b, #8
	eor		T1.16b, NEXT.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_p8	XH, XL, SHASH, 1
	eor		T1.16b, T1.16b, XL.16b
	__pmull_p8	XL, XL, SHASH, 1
	__pmull_p8	XM, T1, SHASH2, 1

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	/* The last reduction must not read beyond the supplied input span. */
	cbz		w0, .Lghash_gpr_last
	__pmull_reduce_p8_sched 1
	b		.Lghash_gpr_loop

.Lghash_gpr_last:
	__pmull_reduce_p8_sched
	st1		{XL.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_gpr_blocks)

	/*
	 * XOR two independent 64x64 polynomial products. The low/high lanes
	 * of ad and SHASH hold one operand pair each. Combine corresponding
	 * byte products before their shared, linear reconstruction. ss1..ss4
	 * are scratch here; key rotations arrive precomputed from setkey.
	 */
	.macro		__pmull_2x_p8, rq, ad, load_key=1, next_key=0, combine=1
	.if		\load_key
	ld1		{SHASH.2d}, [x4], #16
	ld1		{sh1.2d-sh4.2d}, [x4], #64
	.endif
	tbl		t3.16b, {\ad\().16b}, perm1.16b
	.if		\next_key
	ldr		x6, [x4]
	.endif
	tbl		t5.16b, {\ad\().16b}, perm2.16b
	.if		\next_key
	ldr		x7, [x4, #8]
	.endif
	tbl		t7.16b, {\ad\().16b}, perm3.16b
	.if		\next_key
	ldr		x8, [x4, #24]
	.endif

	pmull2		ss1.8h, \ad\().16b, sh1.16b
	.if		\next_key
	ldr		x9, [x4, #40]
	.endif
	pmull		t4.8h, \ad\().8b, sh1.8b
	.if		\next_key
	ldr		x10, [x4, #56]
	.endif
	pmull2		ss2.8h, t3.16b, SHASH.16b
	.if		\next_key
	ldr		x11, [x4, #72]
	.endif
	pmull		t3.8h, t3.8b, SHASH.8b
	pmull2		ss3.8h, \ad\().16b, sh2.16b
	pmull		t6.8h, \ad\().8b, sh2.8b
	pmull2		ss4.8h, t5.16b, SHASH.16b
	pmull		t5.8h, t5.8b, SHASH.8b
	eor		t4.16b, t4.16b, ss1.16b
	eor		t3.16b, t3.16b, ss2.16b
	pmull2		ss1.8h, \ad\().16b, sh3.16b
	pmull		t8.8h, \ad\().8b, sh3.8b
	pmull2		ss2.8h, t7.16b, SHASH.16b
	pmull		t7.8h, t7.8b, SHASH.8b
	eor		t6.16b, t6.16b, ss3.16b
	eor		t5.16b, t5.16b, ss4.16b
	pmull2		ss3.8h, \ad\().16b, sh4.16b
	pmull		t9.8h, \ad\().8b, sh4.8b
	pmull2		ss4.8h, \ad\().16b, SHASH.16b
	pmull		\rq\().8h, \ad\().8b, SHASH.8b
	.if		\next_key
	/*
	 * The old table is dead. Pair each low-half load with an independent
	 * lane insert. LDR D clears the high half, so insert it afterwards.
	 * SHASH receives both halves from GPRs; sh1..sh4 receive their high
	 * halves from GPRs and their low halves directly from the next table.
	 */
	ldr		d20, [x4, #16]
	mov		SHASH.d[0], x6
	ldr		d21, [x4, #32]
	mov		sh1.d[1], x8
	ldr		d22, [x4, #48]
	mov		sh2.d[1], x9
	ldr		d23, [x4, #64]
	mov		sh3.d[1], x10
	mov		SHASH.d[1], x7
	mov		sh4.d[1], x11
	.endif
	eor		t8.16b, t8.16b, ss1.16b
	.if		\next_key
	add		x4, x4, #80
	.endif
	eor		t7.16b, t7.16b, ss2.16b
	eor		t9.16b, t9.16b, ss3.16b
	eor		\rq\().16b, \rq\().16b, ss4.16b
	eor		t3.16b, t3.16b, t4.16b
	eor		t5.16b, t5.16b, t6.16b
	eor		t7.16b, t7.16b, t8.16b
	.if		\combine
	__pmull_p8_combine \rq
	.endif
	.endm

	/*
	 * Reflected Montgomery reduction using byte PMULL by 0xc2.
	 * After the Karatsuba join, XL holds the low 128 product bits.
	 * Split each byte product into low (t5) and high (t6) bytes. Rotating
	 * t5 right by one byte combines adjacent products, with its wrapped
	 * byte supplying the second fold's high-byte correction. The third
	 * PMULL supplies that correction's remaining two bytes at bits 112..127.
	 * The constant register holds replicated 0xc2. Pair loops use SHASH2;
	 * scheduled single-block loops prepare T2 after their Karatsuba products.
	 * The original loop keeps the constant in otherwise unused v28.
	 */
	.macro		__pmull_reduce_p8_mul, c=SHASH2, masked=0, permute=0, \
			last=REDUCE_LAST, perm=REDUCE_PERM
	eor		XM.16b, XM.16b, T1.16b
	zip1		XL.2d, XL.2d, XM.2d
	zip2		XH.2d, XM.2d, XH.2d

	pmull		t3.8h, XL.8b, \c\().8b
	pmull2		t4.8h, XL.16b, \c\().16b
	eor		XH.16b, XH.16b, XL.16b
	.if		\permute
	tbl		t5.16b, {t3.16b, t4.16b}, \perm\().16b
	.else
	uzp1		t5.16b, t3.16b, t4.16b
	.endif
	uzp2		t6.16b, t3.16b, t4.16b
	.if		\masked
	.if		!\permute
	ext		t5.16b, t5.16b, t5.16b, #1
	.endif
	pmull2		t7.8h, t5.16b, \last\().16b
	.else
	pmull		t7.8h, t5.8b, \c\().8b
	ext		t5.16b, t5.16b, t5.16b, #1
	movi		t8.16b, #0
	.endif
	eor		t6.16b, t6.16b, t5.16b
	.if		!\masked
	ext		t7.16b, t8.16b, t7.16b, #2
	.endif
	eor		XH.16b, XH.16b, t6.16b
	eor		XL.16b, XH.16b, t7.16b
	.endm

	.macro		__pmull_reduce_p8_direct
	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	eor		T1.16b, T1.16b, XM.16b

	pmull		t3.8h, XL.8b, REDUCE_CONST.8b
	pmull		t4.8h, T1.8b, REDUCE_CONST.8b
	ext		XM.16b, XM.16b, XM.16b, #8
	eor		XH.16b, T2.16b, XM.16b
	uzp1		t5.16b, t3.16b, t4.16b
	uzp2		t6.16b, t3.16b, t4.16b
	ext		t5.16b, t5.16b, t5.16b, #1
	pmull2		t7.8h, t5.16b, REDUCE_LAST.16b
	eor		t6.16b, t6.16b, t5.16b
	eor		XH.16b, XH.16b, t6.16b
	eor		XL.16b, XH.16b, t7.16b
	.endm

	/* (Y ^ X1) * H^2 ^ X2 * H, with one reduction for each block pair. */
	.macro		__ghash_2x, mul_reduce=0
	cmp		w0, #2
	b.lo		ghash_neon_sched_blocks
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 0
	.if		\mul_reduce
	movi		SHASH2.16b, #0xc2
	.endif

1:
	ld1		{T1.2d}, [x2], #16
	ld1		{NEXT.2d}, [x2], #16
CPU_LE(	rev64		T1.16b, T1.16b	)
CPU_LE(	rev64		NEXT.16b, NEXT.16b )
	ext		IN1.16b, T1.16b, T1.16b, #8
	ext		NEXT.16b, NEXT.16b, NEXT.16b, #8
	eor		IN1.16b, IN1.16b, XL.16b
	/* Pack corresponding halves of the two field elements into lanes. */
	zip1		T1.2d, IN1.2d, NEXT.2d
	zip2		T2.2d, IN1.2d, NEXT.2d
	eor		NEXT.16b, T1.16b, T2.16b
	/* The context starts with h[2], then three 80-byte key tables. */
	add		x4, x3, #16
	__pmull_2x_p8	XL, T1		// sum of the two low products
	__pmull_2x_p8	XH, T2		// sum of the two high products
	__pmull_2x_p8	XM, NEXT	// sum of the two cross products

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	.if		\mul_reduce
	__pmull_reduce_p8_mul
	.else
	__pmull_reduce_p8_sched
	.endif
	sub		w0, w0, #2
	cmp		w0, #2
	b.hs		1b
	st1		{XL.2d}, [x1]
	/* Preserve the original key pointer for a final single block. */
	cbnz		w0, ghash_neon_sched_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_2x_blocks)
	__ghash_2x
SYM_FUNC_END(ghash_neon_2x_blocks)

SYM_FUNC_START(ghash_neon_2x_pmull_blocks)
	__ghash_2x 1
SYM_FUNC_END(ghash_neon_2x_pmull_blocks)

SYM_FUNC_START(ghash_neon_pmull_blocks)
	__ghash_sched 1
SYM_FUNC_END(ghash_neon_pmull_blocks)

	/* Stock p8 input loads, products and shuffles; change only reduction. */
	.macro		__ghash_orig_pmull, masked=0, permute=0, direct=0
	ld1		{SHASH.2d}, [x3]
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 1, 1
	/* Original products do not need the fused-shuffle table registers. */
	movi		REDUCE_CONST.16b, #0xc2
	.if		\masked
	movi		REDUCE_LAST.16b, #0
	mov		REDUCE_LAST.b[15], REDUCE_CONST.b[0]
	.endif
	.if		\permute
	adr_l		x5, .Lghash_neon_reduce_perm
	ld1		{REDUCE_PERM.16b}, [x5]
	.endif

.Lghash_orig_pmull_loop\@:
	ld1		{T1.2d}, [x2], #16
	sub		w0, w0, #1
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_p8	XH, XL, SHASH, 0, 1
	eor		T1.16b, T1.16b, XL.16b
	__pmull_p8	XL, XL, SHASH, 0, 1
	__pmull_p8	XM, T1, SHASH2, 0, 1

	.if		\direct
	__pmull_reduce_p8_direct
	.else
	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	__pmull_reduce_p8_mul REDUCE_CONST, \masked, \permute
	.endif
	cbnz		w0, .Lghash_orig_pmull_loop\@

	st1		{XL.2d}, [x1]
	ret
	.endm

SYM_FUNC_START(ghash_neon_orig_pmull_blocks)
	__ghash_orig_pmull
SYM_FUNC_END(ghash_neon_orig_pmull_blocks)

SYM_FUNC_START(ghash_neon_orig_pmull13_blocks)
	__ghash_orig_pmull 1
SYM_FUNC_END(ghash_neon_orig_pmull13_blocks)

SYM_FUNC_START(ghash_neon_orig_pmull12_blocks)
	__ghash_orig_pmull 1, 1
SYM_FUNC_END(ghash_neon_orig_pmull12_blocks)

SYM_FUNC_START(ghash_neon_orig_direct_blocks)
	__ghash_orig_pmull 1, 0, 1
SYM_FUNC_END(ghash_neon_orig_direct_blocks)

	/*
	 * Fold the first Montgomery step into the fixed key at setkey().
	 * For t = x^64, c = 0xc200000000000000 and P = t^2 + c*t + 1:
	 * K = H0*(t + c) + H1, so A*H/t = A0*K + A1*H (mod P).
	 * Form L = A0*K0 + A1*H0 and U = A0*K1 + A1*H1. Then
	 * A*H/t^2 = L0*c + swap64(L) + U (mod P).
	 * All additions and multiplications are polynomial operations.
	 */
SYM_FUNC_START(ghash_neon_fused_blocks)
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 0
	/* These two registers are not touched by the paired product helper. */
	movi		SHASH2.16b, #0xc2
	movi		T2.16b, #0

.Lghash_fused_loop:
	ld1		{T1.2d}, [x2], #16
CPU_LE(	rev64		T1.16b, T1.16b	)
	ext		T1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, XL.16b
	/* Two 80-byte tables, with K in low lanes and H in high lanes. */
	add		x4, x3, #16
	__pmull_2x_p8	XL, T1		// L = A0*K0 + A1*H0
	__pmull_2x_p8	XH, T1		// U = A0*K1 + A1*H1

	/* One fold: multiply L0 by 0xc2, then shift the product by 56. */
	pmull		t3.8h, XL.8b, SHASH2.8b
	uzp1		t4.16b, t3.16b, T2.16b
	uzp2		t5.16b, t3.16b, T2.16b
	ext		t4.16b, T2.16b, t4.16b, #9
	ext		t5.16b, T2.16b, t5.16b, #8
	eor		t4.16b, t4.16b, t5.16b
	ext		XM.16b, XL.16b, XL.16b, #8
	eor		XH.16b, XH.16b, XM.16b
	eor		XL.16b, XH.16b, t4.16b
	sub		w0, w0, #1
	cbnz		w0, .Lghash_fused_loop

	st1		{XL.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_fused_blocks)

	/* The cached key table in SHASH/sh1..sh4 survives the final fold. */
	.macro		__ghash_2x_reuse_step, reverse=0, pipe_key=0
	ld1		{T1.2d}, [x2], #16
	ld1		{NEXT.2d}, [x2], #16
CPU_LE(	rev64		T1.16b, T1.16b	)
CPU_LE(	rev64		NEXT.16b, NEXT.16b )
	ext		IN1.16b, T1.16b, T1.16b, #8
	ext		NEXT.16b, NEXT.16b, NEXT.16b, #8
	eor		IN1.16b, IN1.16b, XL.16b
	zip1		T1.2d, IN1.2d, NEXT.2d
	zip2		T2.2d, IN1.2d, NEXT.2d
	eor		NEXT.16b, T1.16b, T2.16b
	.if		\reverse
	/* Cross is cached; x4 is just beyond the three 80-byte tables. */
	.if		\pipe_key
	sub		x4, x4, #160
	__pmull_2x_p8	XM, NEXT, 0, 1
	sub		x4, x4, #160
	__pmull_2x_p8	XH, T2, 0, 1
	__pmull_2x_p8	XL, T1, 0
	.else
	__pmull_2x_p8	XM, NEXT, 0
	sub		x4, x4, #160
	__pmull_2x_p8	XH, T2
	sub		x4, x4, #160
	__pmull_2x_p8	XL, T1
	.endif
	.else
	/* Low is cached; x4 points to the high-product table. */
	.if		\pipe_key
	__pmull_2x_p8	XL, T1, 0, 1
	__pmull_2x_p8	XH, T2, 0, 1
	__pmull_2x_p8	XM, NEXT, 0
	.else
	__pmull_2x_p8	XL, T1, 0
	__pmull_2x_p8	XH, T2
	__pmull_2x_p8	XM, NEXT
	.endif
	.endif

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	__pmull_reduce_p8_mul
	sub		w0, w0, #2
	cmp		w0, #2
	.endm

	/* Alternate low/high/cross and cross/high/low without changing the math. */
	.macro		__ghash_2x_reuse, pipe_key=0
	cmp		w0, #2
	b.lo		ghash_neon_sched_blocks
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 0
	movi		SHASH2.16b, #0xc2
	add		x4, x3, #16
	ld1		{SHASH.2d}, [x4], #16
	ld1		{sh1.2d-sh4.2d}, [x4], #64

1:
	__ghash_2x_reuse_step 0, \pipe_key
	b.lo		2f
	__ghash_2x_reuse_step 1, \pipe_key
	b.hs		1b
2:
	st1		{XL.2d}, [x1]
	/* As in 2x-pmull, a single block or odd tail uses the scheduled loop. */
	cbnz		w0, ghash_neon_sched_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_2x_reuse_blocks)
	__ghash_2x_reuse
SYM_FUNC_END(ghash_neon_2x_reuse_blocks)

SYM_FUNC_START(ghash_neon_2x_load_blocks)
	__ghash_2x_reuse 1
SYM_FUNC_END(ghash_neon_2x_load_blocks)

	/* Stage the next input group using independent scalar instructions. */
	.macro		__ghash_stage, n, enable=1
	.if		\enable
	.if		(\n % 13) == 0
	ldr		x6, [x12, #(8 + (\n / 13) * 32)]
	.elseif		(\n % 13) == 1
	ldr		x7, [x12, #((\n / 13) * 32)]
	.elseif		(\n % 13) == 2
	ldr		x8, [x12, #(24 + (\n / 13) * 32)]
	.elseif		(\n % 13) == 3
	ldr		x9, [x12, #(16 + (\n / 13) * 32)]
	.elseif		(\n % 13) == 4
CPU_LE(	rev		x6, x6	)
	.elseif		(\n % 13) == 5
CPU_LE(	rev		x7, x7	)
	.elseif		(\n % 13) == 6
CPU_LE(	rev		x8, x8	)
	.elseif		(\n % 13) == 7
CPU_LE(	rev		x9, x9	)
	.elseif		(\n % 13) == 8
	stp		x6, x8, [sp, #((\n / 13) * 32)]
	.elseif		(\n % 13) == 9
	stp		x7, x9, [sp, #(16 + (\n / 13) * 32)]
	.elseif		(\n % 13) == 10
	eor		x6, x6, x7
	.elseif		(\n % 13) == 11
	eor		x8, x8, x9
	.else
	stp		x6, x8, [sp, #(64 + (\n / 13) * 16)]
	.endif
	.endif
	.endm

	/*
	 * Add two more 64x64 products to the unreconstructed t3/t5/t7/t9/rq
	 * terms left by __pmull_2x_p8. Only t4/t6/t8 and ss1..ss4 are scratch:
	 * the other six data registers hold inputs or completed products.
	 * Reconstruct once after the last pair; combine=0 retains the raw sum
	 * so the eight-block path can accumulate further pairs.
	 */
	.macro		__pmull_4x_acc_p8, rq, ad, next_key=0, combine=1, stage=0
	.if		\next_key && \stage
	.error		"Input and key staging share GPR scratch registers"
	.endif
	tbl		ss1.16b, {\ad\().16b}, perm1.16b
	__ghash_stage	0, \stage
	.if		\next_key
	ldr		x6, [x4]
	.endif
	tbl		ss2.16b, {\ad\().16b}, perm2.16b
	__ghash_stage	1, \stage
	.if		\next_key
	ldr		x7, [x4, #8]
	.endif
	tbl		ss3.16b, {\ad\().16b}, perm3.16b
	__ghash_stage	2, \stage
	.if		\next_key
	ldr		x8, [x4, #24]
	.endif
	pmull2		t4.8h, \ad\().16b, sh1.16b
	__ghash_stage	3, \stage
	.if		\next_key
	ldr		x9, [x4, #40]
	.endif
	pmull		t6.8h, \ad\().8b, sh1.8b
	__ghash_stage	4, \stage
	.if		\next_key
	ldr		x10, [x4, #56]
	.endif
	pmull2		t8.8h, ss1.16b, SHASH.16b
	__ghash_stage	5, \stage
	.if		\next_key
	ldr		x11, [x4, #72]
	.endif
	pmull		ss1.8h, ss1.8b, SHASH.8b
	__ghash_stage	6, \stage
	eor		t4.16b, t4.16b, t6.16b
	__ghash_stage	7, \stage
	eor		t8.16b, t8.16b, ss1.16b
	__ghash_stage	8, \stage
	pmull2		ss1.8h, \ad\().16b, sh2.16b
	__ghash_stage	9, \stage
	pmull		t6.8h, \ad\().8b, sh2.8b
	__ghash_stage	10, \stage
	eor		t4.16b, t4.16b, t8.16b
	__ghash_stage	11, \stage
	pmull2		t8.8h, ss2.16b, SHASH.16b
	__ghash_stage	12, \stage
	pmull		ss2.8h, ss2.8b, SHASH.8b
	__ghash_stage	13, \stage
	eor		t3.16b, t3.16b, t4.16b
	__ghash_stage	14, \stage
	eor		ss1.16b, ss1.16b, t6.16b
	__ghash_stage	15, \stage
	eor		ss2.16b, ss2.16b, t8.16b
	__ghash_stage	16, \stage
	pmull2		t4.8h, \ad\().16b, sh3.16b
	__ghash_stage	17, \stage
	pmull		t6.8h, \ad\().8b, sh3.8b
	__ghash_stage	18, \stage
	eor		ss1.16b, ss1.16b, ss2.16b
	__ghash_stage	19, \stage
	pmull2		t8.8h, ss3.16b, SHASH.16b
	__ghash_stage	20, \stage
	pmull		ss3.8h, ss3.8b, SHASH.8b
	__ghash_stage	21, \stage
	eor		t5.16b, t5.16b, ss1.16b
	__ghash_stage	22, \stage
	eor		t4.16b, t4.16b, t6.16b
	__ghash_stage	23, \stage
	eor		ss3.16b, ss3.16b, t8.16b
	__ghash_stage	24, \stage
	pmull2		ss1.8h, \ad\().16b, sh4.16b
	__ghash_stage	25, \stage
	pmull		ss2.8h, \ad\().8b, sh4.8b
	pmull2		t6.8h, \ad\().16b, SHASH.16b
	pmull		t8.8h, \ad\().8b, SHASH.8b
	.if		\next_key
	/* Retain the measured GPR/D-load schedule once the old key is dead. */
	ldr		d20, [x4, #16]
	mov		SHASH.d[0], x6
	ldr		d21, [x4, #32]
	mov		sh1.d[1], x8
	ldr		d22, [x4, #48]
	mov		sh2.d[1], x9
	ldr		d23, [x4, #64]
	mov		sh3.d[1], x10
	mov		SHASH.d[1], x7
	mov		sh4.d[1], x11
	.endif
	eor		t4.16b, t4.16b, ss3.16b
	.if		\next_key
	add		x4, x4, #80
	.endif
	eor		ss1.16b, ss1.16b, ss2.16b
	eor		t6.16b, t6.16b, t8.16b
	eor		t7.16b, t7.16b, t4.16b
	eor		t9.16b, t9.16b, ss1.16b
	eor		\rq\().16b, \rq\().16b, t6.16b
	.if		\combine
	__pmull_p8_combine \rq
	.endif
	.endm

	/* (Y ^ X1)*H^4 ^ X2*H^3 ^ X3*H^2 ^ X4*H, reduced once. */
	.macro		__ghash_4x_step, reverse=0, short_reduce=0, stage=0
	.if		\stage
	movi		t3.16b, #0
	zip1		t4.2d, XL.2d, t3.2d
	zip2		t5.2d, XL.2d, t3.2d
	eor		t6.16b, t4.16b, t5.16b
	.if		\reverse
	ldp		q6, q7, [sp, #32]
	ldp		q2, q3, [sp]
	ldp		q4, q5, [sp, #64]
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
CPU_BE(	ext		v4.16b, v4.16b, v4.16b, #8	)
CPU_BE(	ext		v5.16b, v5.16b, v5.16b, #8	)
CPU_BE(	ext		v6.16b, v6.16b, v6.16b, #8	)
CPU_BE(	ext		v7.16b, v7.16b, v7.16b, #8	)
	eor		T1.16b, T1.16b, t4.16b
	eor		T2.16b, T2.16b, t5.16b
	eor		NEXT.16b, NEXT.16b, t6.16b
	.else
	ldp		q6, q7, [sp]
	ldp		q2, q3, [sp, #32]
	ldp		q5, q4, [sp, #64]
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
CPU_BE(	ext		v4.16b, v4.16b, v4.16b, #8	)
CPU_BE(	ext		v5.16b, v5.16b, v5.16b, #8	)
CPU_BE(	ext		v6.16b, v6.16b, v6.16b, #8	)
CPU_BE(	ext		v7.16b, v7.16b, v7.16b, #8	)
	eor		XL.16b, XL.16b, t4.16b
	eor		XH.16b, XH.16b, t5.16b
	eor		XM.16b, XM.16b, t6.16b
	.endif
	/* The final group stages itself again, keeping every load in bounds. */
	mov		x12, x2
	add		x2, x2, #64
	cmp		w0, #8
	csel		x12, x2, x12, hs
	.else
	ld1		{t3.2d-t6.2d}, [x2], #64
CPU_LE(	rev64		t3.16b, t3.16b	)
CPU_LE(	rev64		t4.16b, t4.16b	)
CPU_LE(	rev64		t5.16b, t5.16b	)
CPU_LE(	rev64		t6.16b, t6.16b	)
	ext		t3.16b, t3.16b, t3.16b, #8
	ext		t4.16b, t4.16b, t4.16b, #8
	ext		t5.16b, t5.16b, t5.16b, #8
	ext		t6.16b, t6.16b, t6.16b, #8
	eor		t3.16b, t3.16b, XL.16b
	.if		\reverse
	/* Reverse the term pairs too, keeping the first pair in XL/XH/XM. */
	zip1		XL.2d, t5.2d, t6.2d
	zip2		XH.2d, t5.2d, t6.2d
	zip1		T1.2d, t3.2d, t4.2d
	zip2		T2.2d, t3.2d, t4.2d
	.else
	zip1		XL.2d, t3.2d, t4.2d
	zip2		XH.2d, t3.2d, t4.2d
	zip1		T1.2d, t5.2d, t6.2d
	zip2		T2.2d, t5.2d, t6.2d
	.endif
	eor		XM.16b, XL.16b, XH.16b
	eor		NEXT.16b, T1.16b, T2.16b
	.endif
	.if		\reverse
	/* Walk the six tables backwards, retaining the last one for reuse. */
	sub		x4, x4, #160
	__pmull_2x_p8	XM, XM, 0, 1, 0
	sub		x4, x4, #160
	__pmull_4x_acc_p8 XM, NEXT, 1
	sub		x4, x4, #160
	__pmull_2x_p8	XH, XH, 0, 1, 0
	sub		x4, x4, #160
	__pmull_4x_acc_p8 XH, T2, 1
	sub		x4, x4, #160
	__pmull_2x_p8	XL, XL, 0, 1, 0
	__pmull_4x_acc_p8 XL, T1, 0, 1, \stage
	.else
	__pmull_2x_p8	XL, XL, 0, 1, 0
	__pmull_4x_acc_p8 XL, T1, 1
	__pmull_2x_p8	XH, XH, 0, 1, 0
	__pmull_4x_acc_p8 XH, T2, 1
	__pmull_2x_p8	XM, XM, 0, 1, 0
	__pmull_4x_acc_p8 XM, NEXT, 0, 1, \stage
	.endif

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	.if		\short_reduce == 1
	movi		T2.16b, #0xc2
	dup		NEXT.2d, x12
	__pmull_reduce_p8_mul T2, 1, 1, NEXT, SHASH2
	.elseif		\short_reduce == 2
	shl		NEXT.2d, SHASH2.2d, #56
	__pmull_reduce_p8_mul SHASH2, 1, 0, NEXT
	.elseif		\short_reduce == 3
	eor		XM.16b, XM.16b, T1.16b
	zip1		XL.2d, XL.2d, XM.2d
	zip2		XH.2d, XM.2d, XH.2d
	umov		x6, XL.d[0]
	umov		x7, XL.d[1]
	umov		x8, XH.d[0]
	umov		x9, XH.d[1]
	eor		x10, x7, x6, lsl #57
	eor		x8, x8, x7, lsl #57
	eor		x10, x10, x6, lsl #62
	eor		x8, x8, x7, lsl #62
	eor		x10, x10, x6, lsl #63
	eor		x8, x8, x7, lsl #63
	eor		x9, x9, x10
	eor		x8, x8, x6
	eor		x9, x9, x10, lsr #1
	eor		x8, x8, x6, lsr #1
	eor		x9, x9, x10, lsr #2
	eor		x8, x8, x6, lsr #2
	eor		x9, x9, x10, lsr #7
	eor		x8, x8, x6, lsr #7
	fmov		d6, x8
	mov		XL.d[1], x9
	.else
	__pmull_reduce_p8_mul
	.endif
	sub		w0, w0, #4
	cmp		w0, #4
	.endm

	.macro		__ghash_4x, short_reduce=0, stage=0
	cmp		w0, #4
	b.lo		ghash_neon_2x_load_blocks
	.if		\stage
	sub		sp, sp, #96
	mov		x12, x2
	.irp		n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25
	__ghash_stage	\n
	.endr
	.endif
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 0
	.if		\short_reduce == 1
	mov		x12, #0xc200000000000000
	adr_l		x5, .Lghash_neon_reduce_perm
	ld1		{SHASH2.16b}, [x5]
	.else
	movi		SHASH2.16b, #0xc2
	.endif
	/* The first 256 bytes are the unchanged two-block tail context. */
	add		x4, x3, #256
	ld1		{SHASH.2d}, [x4], #16
	ld1		{sh1.2d-sh4.2d}, [x4], #64
1:
	__ghash_4x_step 0, \short_reduce, \stage
	b.lo		2f
	__ghash_4x_step 1, \short_reduce, \stage
	b.hs		1b
2:
	st1		{XL.2d}, [x1]
	.if		\stage
	add		sp, sp, #96
	.endif
	cbnz		w0, ghash_neon_2x_load_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_4x_blocks)
	__ghash_4x
SYM_FUNC_END(ghash_neon_4x_blocks)

SYM_FUNC_START(ghash_neon_4x_gpr_reduce_blocks)
	__ghash_4x 3
SYM_FUNC_END(ghash_neon_4x_gpr_reduce_blocks)

SYM_FUNC_START(ghash_neon_4x_stage_blocks)
	__ghash_4x 2, 1
SYM_FUNC_END(ghash_neon_4x_stage_blocks)

	/*
	 * LD2 packs the native-endian high and low halves of two GHASH blocks
	 * into T1 and T2. Select one Karatsuba operand pair, then reflect it.
	 * SHASH2 holds Y until all three unreduced products are complete.
	 */
	.macro		__ghash_8x_input, part, first=0, reverse=0
	.if		\reverse
	ld2		{T1.2d, T2.2d}, [x15], x16
	.else
	ld2		{T1.2d, T2.2d}, [x15], #32
	.endif
	.if		\part == 0
	mov		T1.16b, T2.16b
	.elseif		\part == 2
	eor		T1.16b, T1.16b, T2.16b
	.endif
CPU_LE(	rev64		T1.16b, T1.16b	)
	.if		\first
	.if		\part == 2
	ext		NEXT.16b, SHASH2.16b, SHASH2.16b, #8
	eor		NEXT.8b, NEXT.8b, SHASH2.8b
	.else
	/* Scalar DUP clears the upper lane, leaving X2 unchanged. */
	dup		d4, SHASH2.d[\part]
	.endif
	eor		T1.16b, T1.16b, NEXT.16b
	.endif
	.endm

	/* Accumulate eight 64x64 products, then reconstruct their sum once. */
	.macro		__ghash_8x_part, rq, part, reverse=0, next_part=1
	.if		\reverse
	add		x15, x2, #96
	.else
	mov		x15, x2
	.endif
	__ghash_8x_input \part, (1 - \reverse), \reverse
	.if		\reverse
	sub		x4, x4, #160
	.endif
	__pmull_2x_p8	\rq, T1, 0, 1, 0
	.rept		2
	__ghash_8x_input \part, 0, \reverse
	.if		\reverse
	sub		x4, x4, #160
	.endif
	__pmull_4x_acc_p8 \rq, T1, 1, 0
	.endr
	__ghash_8x_input \part, \reverse, \reverse
	.if		\reverse && \next_part
	sub		x4, x4, #160
	.endif
	__pmull_4x_acc_p8 \rq, T1, \next_part
	.endm

	/* (Y ^ X1)*H^8 ^ X2*H^7 ^ ... ^ X8*H, reduced once. */
	.macro		__ghash_8x_step, reverse=0
	mov		SHASH2.16b, XL.16b
	.if		\reverse
	__ghash_8x_part	XM, 2, 1
	__ghash_8x_part	XH, 1, 1
	__ghash_8x_part	XL, 0, 1, 0
	.else
	__ghash_8x_part	XL, 0
	__ghash_8x_part	XH, 1
	__ghash_8x_part	XM, 2, 0, 0
	.endif
	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	movi		SHASH2.16b, #0xc2
	__pmull_reduce_p8_mul
	add		x2, x2, #128
	sub		w0, w0, #8
	cmp		w0, #8
	.endm

SYM_FUNC_START(ghash_neon_8x_blocks)
	cmp		w0, #8
	b.lo		ghash_neon_4x_blocks
	ld1		{XL.2d}, [x1]
	__pmull_pre_p8 0
	/* Keep the four-block key/context prefix for the one-to-seven tail. */
	add		x4, x3, #736
	mov		x16, #-32
	ld1		{SHASH.2d}, [x4], #16
	ld1		{sh1.2d-sh4.2d}, [x4], #64
1:
	__ghash_8x_step
	b.lo		2f
	__ghash_8x_step 1
	b.hs		1b
2:
	st1		{XL.2d}, [x1]
	cbnz		w0, ghash_neon_4x_blocks
	ret
SYM_FUNC_END(ghash_neon_8x_blocks)

SYM_FUNC_START(ghash_neon_4x_pmull12_blocks)
	__ghash_4x 1
SYM_FUNC_END(ghash_neon_4x_pmull12_blocks)

SYM_FUNC_START(ghash_neon_4x_pmull13_blocks)
	__ghash_4x 2
SYM_FUNC_END(ghash_neon_4x_pmull13_blocks)

	.section	".rodata", "a"
	.align		6
.Lghash_neon_pack:
	/* {t3, t4}: ZIP1(t4, t3), then rotate left by one byte. */
	.byte		7, 16, 17, 18, 19, 20, 21, 22
	.byte		23, 0, 1, 2, 3, 4, 5, 6
	/* {t3, t4}: ZIP2(t4, t3), then rotate left by two bytes. */
	.byte		14, 15, 24, 25, 26, 27, 28, 29
	.byte		30, 31, 8, 9, 10, 11, 12, 13
	/* {t6, t7}: ZIP1(t6, t7), then rotate left by three bytes. */
	.byte		21, 22, 23, 0, 1, 2, 3, 4
	.byte		5, 6, 7, 16, 17, 18, 19, 20
	/* {t6, t7}: ZIP2(t6, t7), then rotate left by four bytes. */
	.byte		28, 29, 30, 31, 8, 9, 10, 11
	.byte		12, 13, 14, 15, 24, 25, 26, 27

.Lghash_neon_reduce_perm:
	.byte		2, 4, 6, 8, 10, 12, 14, 16
	.byte		18, 20, 22, 24, 26, 28, 30, 0

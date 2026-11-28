/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Experimental PMUL and integer UMULL GHASH products.
 * PMUL reconstruction derives from Ard Biesheuvel's ghash-neon-core.S.
 * UMULL uses the holes method from lib/crypto/gf128hash.c.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright 2025 Google LLC
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a

	.macro		__mul_reduce, lo, hi, constant, last
	pmull		v10.8h, \lo\().8b, \constant\().8b
	pmull2		v11.8h, \lo\().16b, \constant\().16b
	eor		\hi\().16b, \hi\().16b, \lo\().16b
	uzp1		v12.16b, v10.16b, v11.16b
	uzp2		v13.16b, v10.16b, v11.16b
	ext		v12.16b, v12.16b, v12.16b, #1
	pmull2		v14.8h, v12.16b, \last\().16b
	eor		v13.16b, v13.16b, v12.16b
	eor		\hi\().16b, \hi\().16b, v13.16b
	eor		\lo\().16b, \hi\().16b, v14.16b
	.endm

/* Each byte pair holds a byte and its bit reversal. */
	.macro		__pmul_product, out, in, b0, b1, b2, b3, b4
	ext		v10.16b, \in\().16b, \in\().16b, #2
	ext		v12.16b, \in\().16b, \in\().16b, #4
	ext		v14.16b, \in\().16b, \in\().16b, #6
	pmul		v10.16b, v10.16b, \b0\().16b
	pmul		v11.16b, \in\().16b, \b1\().16b
	pmul		v12.16b, v12.16b, \b0\().16b
	pmul		v13.16b, \in\().16b, \b2\().16b
	pmul		v14.16b, v14.16b, \b0\().16b
	pmul		v15.16b, \in\().16b, \b3\().16b
	pmul		v16.16b, \in\().16b, \b4\().16b
	pmul		\out\().16b, \in\().16b, \b0\().16b
	eor		v10.16b, v10.16b, v11.16b
	eor		v12.16b, v12.16b, v13.16b
	eor		v14.16b, v14.16b, v15.16b

	uzp1		v11.2d, v10.2d, v12.2d
	uzp2		v10.2d, v10.2d, v12.2d
	uzp1		v13.2d, v14.2d, v16.2d
	uzp2		v14.2d, v14.2d, v16.2d
	eor		v11.16b, v11.16b, v10.16b
	and		v10.16b, v10.16b, v9.16b
	eor		v13.16b, v13.16b, v14.16b
	and		v14.16b, v14.16b, v8.16b
	eor		v11.16b, v11.16b, v10.16b
	eor		v13.16b, v13.16b, v14.16b
	zip2		v12.2d, v11.2d, v10.2d
	zip1		v10.2d, v11.2d, v10.2d
	zip2		v16.2d, v13.2d, v14.2d
	zip1		v14.2d, v13.2d, v14.2d
	ext		v10.16b, v10.16b, v10.16b, #15
	ext		v12.16b, v12.16b, v12.16b, #14
	ext		v14.16b, v14.16b, v14.16b, #13
	ext		v16.16b, v16.16b, v16.16b, #12

/* Keep even/odd byte rotations apart until the high-byte recovery. */
	eor		\out\().16b, \out\().16b, v12.16b
	eor		v16.16b, \out\().16b, v16.16b
	eor		v10.16b, v10.16b, v14.16b
	eor		v11.16b, v16.16b, v10.16b
	and		v11.16b, v11.16b, v0.16b
	eor		v16.16b, v16.16b, v11.16b
	eor		v10.16b, v10.16b, v11.16b
	rbit		v10.16b, v10.16b
	ushr		v10.16b, v10.16b, #1
	eor		\out\().16b, v16.16b, v10.16b
	.endm

SYM_FUNC_START(ghash_neon_pmul_blocks)
	ld1		{v2.2d}, [x3]
	ld1		{v6.2d}, [x1]
	rbit		v3.16b, v2.16b
	zip1		v17.16b, v2.16b, v3.16b
	zip2		v22.16b, v2.16b, v3.16b
	eor		v27.16b, v17.16b, v22.16b
	ext		v18.16b, v17.16b, v17.16b, #2
	ext		v19.16b, v17.16b, v17.16b, #4
	ext		v20.16b, v17.16b, v17.16b, #6
	ext		v21.16b, v17.16b, v17.16b, #8
	ext		v23.16b, v22.16b, v22.16b, #2
	ext		v24.16b, v22.16b, v22.16b, #4
	ext		v25.16b, v22.16b, v22.16b, #6
	ext		v26.16b, v22.16b, v22.16b, #8
	ext		v28.16b, v27.16b, v27.16b, #2
	ext		v29.16b, v27.16b, v27.16b, #4
	ext		v30.16b, v27.16b, v27.16b, #6
	ext		v31.16b, v27.16b, v27.16b, #8
	movi		v9.2d, #0xffffffff
	mov		v9.h[2], v9.h[0]
	ushr		v8.2d, v9.2d, #32
	movi		v0.8h, #0xff, lsl #8
	movi		v1.16b, #0xc2
	movi		v4.16b, #0
	mov		v4.b[15], v1.b[0]

.Lghash_pmul_loop:
	ld1		{v2.2d}, [x2], #16
	sub		w0, w0, #1
CPU_LE(	rev64		v2.16b, v2.16b	)
	ext		v2.16b, v2.16b, v2.16b, #8
	eor		v6.16b, v6.16b, v2.16b
	rbit		v3.16b, v6.16b
	zip2		v7.16b, v6.16b, v3.16b
	zip1		v6.16b, v6.16b, v3.16b
	eor		v2.16b, v6.16b, v7.16b
	__pmul_product	v7, v7, v22, v23, v24, v25, v26
	__pmul_product	v6, v6, v17, v18, v19, v20, v21
	__pmul_product	v5, v2, v27, v28, v29, v30, v31
	eor		v3.16b, v6.16b, v7.16b
	ext		v2.16b, v6.16b, v7.16b, #8
	eor		v5.16b, v5.16b, v3.16b
	eor		v5.16b, v5.16b, v2.16b
	zip1		v6.2d, v6.2d, v5.2d
	zip2		v7.2d, v5.2d, v7.2d
	__mul_reduce	v6, v7, v1, v4
	cbnz		w0, .Lghash_pmul_loop
	st1		{v6.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_pmul_blocks)

/* Two independent clmul32 operations, with four holes masks per operand. */
	.macro		__umull_pair, out, in
	ld1		{v13.2s-v16.2s}, [x4], #32
	and		v9.16b, \in\().16b, v28.16b
	and		v10.16b, \in\().16b, v29.16b
	and		v11.16b, \in\().16b, v30.16b
	and		v12.16b, \in\().16b, v31.16b

	umull		v17.2d, v9.2s, v13.2s
	umull		v21.2d, v10.2s, v16.2s
	eor		v17.16b, v17.16b, v21.16b
	umull		v21.2d, v11.2s, v15.2s
	eor		v17.16b, v17.16b, v21.16b
	umull		v21.2d, v12.2s, v14.2s
	eor		v17.16b, v17.16b, v21.16b
	umull		v18.2d, v9.2s, v14.2s
	umull		v21.2d, v10.2s, v13.2s
	eor		v18.16b, v18.16b, v21.16b
	umull		v21.2d, v11.2s, v16.2s
	eor		v18.16b, v18.16b, v21.16b
	umull		v21.2d, v12.2s, v15.2s
	eor		v18.16b, v18.16b, v21.16b
	umull		v19.2d, v9.2s, v15.2s
	umull		v21.2d, v10.2s, v14.2s
	eor		v19.16b, v19.16b, v21.16b
	umull		v21.2d, v11.2s, v13.2s
	eor		v19.16b, v19.16b, v21.16b
	umull		v21.2d, v12.2s, v16.2s
	eor		v19.16b, v19.16b, v21.16b
	umull		v20.2d, v9.2s, v16.2s
	umull		v21.2d, v10.2s, v15.2s
	eor		v20.16b, v20.16b, v21.16b
	umull		v21.2d, v11.2s, v14.2s
	eor		v20.16b, v20.16b, v21.16b
	umull		v21.2d, v12.2s, v13.2s
	eor		v20.16b, v20.16b, v21.16b
	and		v17.16b, v17.16b, v28.16b
	and		v18.16b, v18.16b, v29.16b
	and		v19.16b, v19.16b, v30.16b
	and		v20.16b, v20.16b, v31.16b
	eor		v17.16b, v17.16b, v18.16b
	eor		v19.16b, v19.16b, v20.16b
	eor		\out\().16b, v17.16b, v19.16b
	.endm

/* The ninth clmul32 packs four masks, using both widening halves. */
	.macro		__umull_single
	ld1		{v13.4s}, [x4]
	dup		v9.4s, v4.s[0]
	and		v9.16b, v9.16b, v26.16b
	ext		v14.16b, v13.16b, v13.16b, #12
	ext		v15.16b, v13.16b, v13.16b, #8
	ext		v16.16b, v13.16b, v13.16b, #4
	umull		v17.2d, v9.2s, v13.2s
	umull2		v21.2d, v9.4s, v13.4s
	eor		v17.16b, v17.16b, v21.16b
	umull		v18.2d, v9.2s, v14.2s
	umull2		v21.2d, v9.4s, v14.4s
	eor		v18.16b, v18.16b, v21.16b
	umull		v19.2d, v9.2s, v15.2s
	umull2		v21.2d, v9.4s, v15.4s
	eor		v19.16b, v19.16b, v21.16b
	umull		v20.2d, v9.2s, v16.2s
	umull2		v21.2d, v9.4s, v16.4s
	eor		v20.16b, v20.16b, v21.16b
	and		v17.16b, v17.16b, v28.16b
	and		v18.16b, v18.16b, v29.16b
	and		v19.16b, v19.16b, v30.16b
	and		v20.16b, v20.16b, v31.16b
	eor		v17.16b, v17.16b, v18.16b
	eor		v19.16b, v19.16b, v20.16b
	eor		v22.16b, v17.16b, v19.16b
	ext		v21.16b, v22.16b, v22.16b, #8
	eor		v22.16b, v22.16b, v21.16b
	.endm

SYM_FUNC_START(ghash_neon_umull_blocks)
	adr_l		x5, .Lghash_umull_masks
	ld1		{v26.4s}, [x5]
	ld1		{v0.2d}, [x1]
	movi		v28.16b, #0x11
	movi		v29.16b, #0x22
	movi		v30.16b, #0x44
	movi		v31.16b, #0x88
	movi		v24.16b, #0xc2
	movi		v25.16b, #0
	mov		v25.b[15], v24.b[0]

.Lghash_umull_loop:
	ld1		{v1.2d}, [x2], #16
	sub		w0, w0, #1
CPU_LE(	rev64		v1.16b, v1.16b	)
	ext		v1.16b, v1.16b, v1.16b, #8
	eor		v0.16b, v0.16b, v1.16b
	ext		v1.16b, v0.16b, v0.16b, #8
	rev64		v2.4s, v0.4s
	eor		v2.16b, v2.16b, v0.16b
	uzp1		v2.4s, v2.4s, v2.4s
	eor		v3.16b, v0.16b, v1.16b
	rev64		v4.4s, v3.4s
	eor		v4.16b, v4.16b, v3.16b
	add		x4, x3, #16
	__umull_pair	v5, v0
	__umull_pair	v6, v1
	__umull_pair	v7, v2
	__umull_pair	v8, v3
	__umull_single

	uzp1		v0.2d, v5.2d, v6.2d
	uzp2		v1.2d, v5.2d, v6.2d
	eor		v7.16b, v7.16b, v0.16b
	eor		v7.16b, v7.16b, v1.16b
	shl		v2.2d, v7.2d, #32
	ushr		v7.2d, v7.2d, #32
	eor		v0.16b, v0.16b, v2.16b
	eor		v1.16b, v1.16b, v7.16b
	ext		v2.16b, v8.16b, v8.16b, #8
	eor		v22.16b, v22.16b, v8.16b
	eor		v22.16b, v22.16b, v2.16b
	shl		v3.2d, v22.2d, #32
	ushr		v4.2d, v22.2d, #32
	eor		v3.16b, v3.16b, v8.16b
	eor		v4.16b, v4.16b, v2.16b
	zip1		v3.2d, v3.2d, v4.2d
	zip2		v2.2d, v0.2d, v1.2d
	zip1		v0.2d, v0.2d, v1.2d
	eor		v1.16b, v0.16b, v2.16b
	ext		v4.16b, v0.16b, v2.16b, #8
	eor		v3.16b, v3.16b, v1.16b
	eor		v3.16b, v3.16b, v4.16b
	zip1		v0.2d, v0.2d, v3.2d
	zip2		v1.2d, v3.2d, v2.2d
	__mul_reduce	v0, v1, v24, v25
	cbnz		w0, .Lghash_umull_loop
	st1		{v0.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_umull_blocks)

	.section	".rodata", "a"
	.align		4
.Lghash_umull_masks:
	.word		0x11111111, 0x22222222, 0x44444444, 0x88888888

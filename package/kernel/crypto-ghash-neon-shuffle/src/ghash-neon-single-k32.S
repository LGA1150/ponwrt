/* SPDX-License-Identifier: GPL-2.0-only */
/* Single-block GHASH with 128/64/32-bit Karatsuba multiplication. */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a

	/* Four independent 32x32 products, two in each result register. */
	.macro		__single_k32_four, a, k0, k1, k2, lo, hi
	tbl		v16.16b, {\a\().16b}, v25.16b
	pmull		\lo\().8h, \a\().8b, \k0\().8b
	pmull2		\hi\().8h, \a\().16b, \k0\().16b
	pmull		v17.8h, \a\().8b, \k1\().8b
	pmull2		v18.8h, \a\().16b, \k1\().16b
	pmull		v19.8h, v16.8b, \k0\().8b
	pmull2		v20.8h, v16.16b, \k0\().16b
	pmull		v21.8h, \a\().8b, \k2\().8b
	pmull2		v22.8h, \a\().16b, \k2\().16b
	eor		v17.16b, v17.16b, v19.16b
	eor		v18.16b, v18.16b, v20.16b
	tbl		v19.16b, {v17.16b}, v26.16b
	tbl		v20.16b, {v18.16b}, v26.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	eor		\lo\().16b, \lo\().16b, v19.16b
	eor		\hi\().16b, \hi\().16b, v20.16b
	rev64		v19.4s, v21.4s
	rev64		v20.4s, v22.4s
	eor		\lo\().16b, \lo\().16b, v23.16b
	eor		\hi\().16b, \hi\().16b, v16.16b
	eor		v21.16b, v21.16b, v19.16b
	eor		v22.16b, v22.16b, v20.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	eor		\lo\().16b, \lo\().16b, v21.16b
	eor		\hi\().16b, \hi\().16b, v22.16b
	.endm

	/* The ninth product uses either four half-full or two full PMULLs. */
	.macro		__single_k32_last, packed
	tbl		v19.16b, {v11.16b}, v25.16b
	pmull		v16.8h, v11.8b, v6.8b
	.if		\packed
	zip1		v19.4s, v11.4s, v19.4s
	pmull		v17.8h, v19.8b, v7.8b
	ext		v18.16b, v17.16b, v17.16b, #8
	eor		v17.16b, v17.16b, v18.16b
	dup		v18.2d, v16.d[1]
	.else
	pmull		v17.8h, v11.8b, v7.8b
	pmull		v18.8h, v19.8b, v6.8b
	eor		v17.16b, v17.16b, v18.16b
	pmull		v18.8h, v11.8b, v8.8b
	.endif
	tbl		v19.16b, {v17.16b}, v26.16b
	tbl		v20.16b, {v17.16b}, v27.16b
	eor		v16.16b, v16.16b, v19.16b
	rev64		v19.4s, v18.4s
	eor		v16.16b, v16.16b, v20.16b
	eor		v18.16b, v18.16b, v19.16b
	tbl		v18.16b, {v18.16b}, v28.16b
	eor		v16.16b, v16.16b, v18.16b
	.endm

	.macro		__ghash_single_k32, packed=0
	cbz		w0, 2f
	adr_l		x4, .Lsingle_k32_masks
	ld1		{v24.16b-v27.16b}, [x4]
	ldr		q28, [x4, #64]
CPU_BE(	rev64		v28.16b, v28.16b	)
CPU_BE(	ext		v28.16b, v28.16b, v28.16b, #8	)
	ld1		{v0.2d}, [x3]
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]

	/* Keep all nine key operands and their rotations in registers. */
	rev64		v3.4s, v0.4s
	ext		v6.16b, v0.16b, v0.16b, #8
	eor		v3.16b, v3.16b, v0.16b
	eor		v6.16b, v6.16b, v0.16b
	uzp1		v3.4s, v3.4s, v3.4s
	zip1		v3.2d, v3.2d, v6.2d
	rev64		v6.4s, v3.4s
	eor		v6.16b, v6.16b, v3.16b
	tbl		v1.16b, {v0.16b}, v25.16b
	rev32		v2.8h, v0.8h
	tbl		v4.16b, {v3.16b}, v25.16b
	rev32		v5.8h, v3.8h
	tbl		v7.16b, {v6.16b}, v25.16b
	rev32		v8.8h, v6.8h
	.if		\packed
	zip1		v7.4s, v7.4s, v6.4s
	zip1		v6.4s, v6.4s, v8.4s
	.endif
1:
	ld1		{v9.2d}, [x2], #16
CPU_LE(	rev64		v9.16b, v9.16b	)
	ext		v9.16b, v9.16b, v9.16b, #8
	eor		v9.16b, v9.16b, v31.16b
	rev64		v10.4s, v9.4s
	ext		v11.16b, v9.16b, v9.16b, #8
	eor		v10.16b, v10.16b, v9.16b
	eor		v11.16b, v11.16b, v9.16b
	uzp1		v10.4s, v10.4s, v10.4s
	zip1		v10.2d, v10.2d, v11.2d
	rev64		v11.4s, v10.4s
	eor		v11.16b, v11.16b, v10.16b

	__single_k32_four v9, v0, v1, v2, v12, v13
	__single_k32_four v10, v3, v4, v5, v14, v15
	__single_k32_last \packed

	/* Reconstruct the three 64x64 products with independent chains. */
	ext		v17.16b, v12.16b, v12.16b, #8
	ext		v18.16b, v13.16b, v13.16b, #8
	ext		v19.16b, v15.16b, v15.16b, #8
	eor		v17.16b, v17.16b, v12.16b
	eor		v18.16b, v18.16b, v13.16b
	eor		v19.16b, v19.16b, v15.16b
	ext		v20.16b, v14.16b, v14.16b, #8
	eor		v17.16b, v17.16b, v14.16b
	eor		v18.16b, v18.16b, v20.16b
	eor		v19.16b, v19.16b, v16.16b
	tbl		v17.16b, {v17.16b}, v24.16b
	tbl		v18.16b, {v18.16b}, v24.16b
	tbl		v19.16b, {v19.16b}, v24.16b
	eor		v12.16b, v12.16b, v17.16b
	eor		v13.16b, v13.16b, v18.16b
	eor		v15.16b, v15.16b, v19.16b

	eor		v17.16b, v12.16b, v13.16b
	ext		v18.16b, v12.16b, v13.16b, #8
	eor		v15.16b, v15.16b, v17.16b
	eor		v15.16b, v15.16b, v18.16b
	zip1		v12.2d, v12.2d, v15.2d
	zip2		v13.2d, v15.2d, v13.2d
	pmull		v16.8h, v12.8b, v29.8b
	pmull2		v17.8h, v12.16b, v29.16b
	eor		v13.16b, v13.16b, v12.16b
	uzp1		v18.16b, v16.16b, v17.16b
	uzp2		v19.16b, v16.16b, v17.16b
	ext		v18.16b, v18.16b, v18.16b, #1
	pmull2		v20.8h, v18.16b, v30.16b
	eor		v19.16b, v19.16b, v18.16b
	eor		v13.16b, v13.16b, v19.16b
	eor		v31.16b, v13.16b, v20.16b
	subs		w0, w0, #1
	b.ne		1b
	st1		{v31.2d}, [x1]
2:
	ret
	.endm

SYM_FUNC_START(ghash_neon_k32_blocks)
	__ghash_single_k32
SYM_FUNC_END(ghash_neon_k32_blocks)

SYM_FUNC_START(ghash_neon_k32_packed_blocks)
	__ghash_single_k32 1
SYM_FUNC_END(ghash_neon_k32_packed_blocks)

	/* Schedule the same nine 32x32 products without expanding the key. */
	.macro		__single_k32_scheduled, preload=0
	eor		v9.16b, v9.16b, v31.16b
	tbl		v16.16b, {v9.16b}, v25.16b
	pmull		v17.8h, v9.8b, v1.8b
	pmull2		v18.8h, v9.16b, v1.16b
	.if		\preload
	ldr		x6, [x2]
	.endif
	pmull		v19.8h, v16.8b, v0.8b
	pmull2		v20.8h, v16.16b, v0.16b
	.if		\preload
	ldr		x7, [x2, #8]
	.endif
	rev64		v10.4s, v9.4s
	ext		v11.16b, v9.16b, v9.16b, #8
	eor		v10.16b, v10.16b, v9.16b
	pmull		v12.8h, v9.8b, v0.8b
	pmull2		v13.8h, v9.16b, v0.16b
	.if		\preload
CPU_LE(	rev		x6, x6	)
	.endif
	eor		v17.16b, v17.16b, v19.16b
	eor		v18.16b, v18.16b, v20.16b
	.if		\preload
CPU_LE(	rev		x7, x7	)
	.endif
	uzp1		v10.4s, v10.4s, v10.4s
	pmull		v21.8h, v9.8b, v2.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	.if		\preload
	add		x2, x2, #16
	.endif
	tbl		v20.16b, {v18.16b}, v26.16b
	eor		v11.16b, v11.16b, v9.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	zip1		v10.2d, v10.2d, v11.2d
	eor		v12.16b, v12.16b, v19.16b
	eor		v13.16b, v13.16b, v20.16b
	rev64		v19.4s, v21.4s
	eor		v13.16b, v13.16b, v16.16b
	tbl		v16.16b, {v10.16b}, v25.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull		v17.8h, v10.8b, v4.8b
	pmull		v19.8h, v16.8b, v3.8b
	pmull2		v22.8h, v9.16b, v2.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	eor		v12.16b, v12.16b, v23.16b
	eor		v12.16b, v12.16b, v21.16b
	.if		\preload
	fmov		d9, x7
	.endif
	pmull		v14.8h, v10.8b, v3.8b
	eor		v17.16b, v17.16b, v19.16b
	.if		\preload
	mov		v9.d[1], x6
	.endif
	rev64		v20.4s, v22.4s
	pmull		v21.8h, v10.8b, v5.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	eor		v22.16b, v22.16b, v20.16b
	eor		v14.16b, v14.16b, v19.16b
	rev64		v11.4s, v10.4s
	pmull2		v18.8h, v10.16b, v4.16b
	pmull2		v20.8h, v16.16b, v3.16b
	rev64		v19.4s, v21.4s
	eor		v11.16b, v11.16b, v10.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull2		v15.8h, v10.16b, v3.16b
	eor		v18.16b, v18.16b, v20.16b
	tbl		v19.16b, {v11.16b}, v25.16b
	tbl		v20.16b, {v18.16b}, v26.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	pmull		v17.8h, v11.8b, v7.8b
	pmull		v18.8h, v19.8b, v6.8b
	eor		v15.16b, v15.16b, v20.16b
	eor		v13.16b, v13.16b, v22.16b
	eor		v15.16b, v15.16b, v16.16b
	pmull2		v22.8h, v10.16b, v5.16b
	pmull		v16.8h, v11.8b, v6.8b
	eor		v17.16b, v17.16b, v18.16b
	pmull		v18.8h, v11.8b, v8.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	rev64		v20.4s, v22.4s
	eor		v16.16b, v16.16b, v19.16b
	rev64		v19.4s, v18.4s
	eor		v22.16b, v22.16b, v20.16b
	tbl		v20.16b, {v17.16b}, v27.16b
	eor		v18.16b, v18.16b, v19.16b
	tbl		v18.16b, {v18.16b}, v28.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	eor		v16.16b, v16.16b, v20.16b
	eor		v14.16b, v14.16b, v23.16b
	eor		v16.16b, v16.16b, v18.16b
	eor		v14.16b, v14.16b, v21.16b
	eor		v15.16b, v15.16b, v22.16b
	ext		v17.16b, v12.16b, v12.16b, #8
	ext		v18.16b, v13.16b, v13.16b, #8
	ext		v19.16b, v15.16b, v15.16b, #8
	ext		v20.16b, v14.16b, v14.16b, #8
	eor		v17.16b, v17.16b, v12.16b
	eor		v18.16b, v18.16b, v13.16b
	eor		v19.16b, v19.16b, v15.16b
	eor		v17.16b, v17.16b, v14.16b
	eor		v18.16b, v18.16b, v20.16b
	eor		v19.16b, v19.16b, v16.16b
	tbl		v17.16b, {v17.16b}, v24.16b
	tbl		v18.16b, {v18.16b}, v24.16b
	tbl		v19.16b, {v19.16b}, v24.16b
	eor		v12.16b, v12.16b, v17.16b
	eor		v13.16b, v13.16b, v18.16b
	eor		v15.16b, v15.16b, v19.16b
	eor		v17.16b, v12.16b, v13.16b
	ext		v18.16b, v12.16b, v13.16b, #8
	eor		v15.16b, v15.16b, v17.16b
	eor		v15.16b, v15.16b, v18.16b
	zip1		v12.2d, v12.2d, v15.2d
	zip2		v13.2d, v15.2d, v13.2d
	pmull		v16.8h, v12.8b, v29.8b
	pmull2		v17.8h, v12.16b, v29.16b
	eor		v13.16b, v13.16b, v12.16b
	uzp1		v18.16b, v16.16b, v17.16b
	uzp2		v19.16b, v16.16b, v17.16b
	ext		v18.16b, v18.16b, v18.16b, #1
	pmull2		v20.8h, v18.16b, v30.16b
	eor		v19.16b, v19.16b, v18.16b
	eor		v13.16b, v13.16b, v19.16b
	eor		v31.16b, v13.16b, v20.16b
	.endm

	.macro		__single_k32_direct, preload=0
	eor		v9.16b, v9.16b, v31.16b
	tbl		v16.16b, {v9.16b}, v25.16b
	pmull		v17.8h, v9.8b, v1.8b
	pmull2		v18.8h, v9.16b, v1.16b
	.if		\preload
	ldr		x6, [x2]
	.endif
	pmull		v19.8h, v16.8b, v0.8b
	pmull2		v20.8h, v16.16b, v0.16b
	.if		\preload
	ldr		x7, [x2, #8]
	.endif
	rev64		v10.4s, v9.4s
	ext		v11.16b, v9.16b, v9.16b, #8
	eor		v10.16b, v10.16b, v9.16b
	pmull		v12.8h, v9.8b, v0.8b
	pmull2		v13.8h, v9.16b, v0.16b
	.if		\preload
CPU_LE(	rev		x6, x6	)
	.endif
	eor		v17.16b, v17.16b, v19.16b
	eor		v18.16b, v18.16b, v20.16b
	.if		\preload
CPU_LE(	rev		x7, x7	)
	.endif
	uzp1		v10.4s, v10.4s, v10.4s
	pmull		v21.8h, v9.8b, v2.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	.if		\preload
	add		x2, x2, #16
	.endif
	tbl		v20.16b, {v18.16b}, v26.16b
	eor		v11.16b, v11.16b, v9.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	zip1		v10.2d, v10.2d, v11.2d
	eor		v12.16b, v12.16b, v19.16b
	eor		v13.16b, v13.16b, v20.16b
	rev64		v19.4s, v21.4s
	eor		v13.16b, v13.16b, v16.16b
	tbl		v16.16b, {v10.16b}, v25.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull		v17.8h, v10.8b, v4.8b
	pmull		v19.8h, v16.8b, v3.8b
	pmull2		v22.8h, v9.16b, v2.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	eor		v12.16b, v12.16b, v23.16b
	eor		v12.16b, v12.16b, v21.16b
	.if		\preload
	fmov		d9, x7
	.endif
	pmull		v14.8h, v10.8b, v3.8b
	eor		v17.16b, v17.16b, v19.16b
	.if		\preload
	mov		v9.d[1], x6
	.endif
	rev64		v20.4s, v22.4s
	pmull		v21.8h, v10.8b, v5.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	eor		v22.16b, v22.16b, v20.16b
	eor		v14.16b, v14.16b, v19.16b
	rev64		v11.4s, v10.4s
	pmull2		v18.8h, v10.16b, v4.16b
	pmull2		v20.8h, v16.16b, v3.16b
	rev64		v19.4s, v21.4s
	eor		v11.16b, v11.16b, v10.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull2		v15.8h, v10.16b, v3.16b
	eor		v18.16b, v18.16b, v20.16b
	tbl		v19.16b, {v11.16b}, v25.16b
	tbl		v20.16b, {v18.16b}, v26.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	pmull		v17.8h, v11.8b, v7.8b
	pmull		v18.8h, v19.8b, v6.8b
	eor		v15.16b, v15.16b, v20.16b
	eor		v13.16b, v13.16b, v22.16b
	eor		v15.16b, v15.16b, v16.16b
	pmull2		v22.8h, v10.16b, v5.16b
	pmull		v16.8h, v11.8b, v6.8b
	eor		v17.16b, v17.16b, v18.16b
	pmull		v18.8h, v11.8b, v8.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	rev64		v20.4s, v22.4s
	eor		v16.16b, v16.16b, v19.16b
	rev64		v19.4s, v18.4s
	eor		v22.16b, v22.16b, v20.16b
	tbl		v20.16b, {v17.16b}, v27.16b
	eor		v18.16b, v18.16b, v19.16b
	tbl		v18.16b, {v18.16b}, v28.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	eor		v16.16b, v16.16b, v20.16b
	eor		v14.16b, v14.16b, v23.16b
	eor		v16.16b, v16.16b, v18.16b
	eor		v14.16b, v14.16b, v21.16b
	eor		v15.16b, v15.16b, v22.16b
	ext		v17.16b, v12.16b, v12.16b, #8
	ext		v18.16b, v13.16b, v13.16b, #8
	ext		v19.16b, v15.16b, v15.16b, #8
	ext		v20.16b, v14.16b, v14.16b, #8
	eor		v17.16b, v17.16b, v12.16b
	eor		v18.16b, v18.16b, v13.16b
	eor		v19.16b, v19.16b, v15.16b
	eor		v17.16b, v17.16b, v14.16b
	eor		v18.16b, v18.16b, v20.16b
	eor		v19.16b, v19.16b, v16.16b
	tbl		v17.16b, {v17.16b}, v24.16b
	tbl		v18.16b, {v18.16b}, v24.16b
	tbl		v19.16b, {v19.16b}, v24.16b
	eor		v12.16b, v12.16b, v17.16b
	eor		v13.16b, v13.16b, v18.16b
	eor		v15.16b, v15.16b, v19.16b
	eor		v17.16b, v12.16b, v13.16b
	ext		v18.16b, v12.16b, v13.16b, #8
	eor		v15.16b, v15.16b, v17.16b
	eor		v18.16b, v18.16b, v15.16b
	pmull		v16.8h, v12.8b, v29.8b
	pmull		v20.8h, v18.8b, v29.8b
	ext		v15.16b, v15.16b, v15.16b, #8
	uzp1		v18.16b, v16.16b, v20.16b
	uzp2		v19.16b, v16.16b, v20.16b
	ext		v18.16b, v18.16b, v18.16b, #1
	eor		v13.16b, v17.16b, v15.16b
	pmull2		v20.8h, v18.16b, v30.16b
	eor		v19.16b, v19.16b, v18.16b
	eor		v13.16b, v13.16b, v19.16b
	eor		v31.16b, v13.16b, v20.16b
	.endm

	.macro		__single_k32_packed_scheduled, preload=0
	eor		v9.16b, v9.16b, v31.16b
	tbl		v16.16b, {v9.16b}, v25.16b
	pmull		v17.8h, v9.8b, v1.8b
	pmull2		v18.8h, v9.16b, v1.16b
	.if		\preload
	ldr		x6, [x2]
	.endif
	pmull		v19.8h, v16.8b, v0.8b
	pmull2		v20.8h, v16.16b, v0.16b
	.if		\preload
	ldr		x7, [x2, #8]
	.endif
	rev64		v10.4s, v9.4s
	ext		v11.16b, v9.16b, v9.16b, #8
	eor		v10.16b, v10.16b, v9.16b
	pmull		v12.8h, v9.8b, v0.8b
	pmull2		v13.8h, v9.16b, v0.16b
	.if		\preload
CPU_LE(	rev		x6, x6	)
	.endif
	eor		v17.16b, v17.16b, v19.16b
	eor		v18.16b, v18.16b, v20.16b
	.if		\preload
CPU_LE(	rev		x7, x7	)
	.endif
	uzp1		v10.4s, v10.4s, v10.4s
	pmull		v21.8h, v9.8b, v2.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	.if		\preload
	add		x2, x2, #16
	.endif
	tbl		v20.16b, {v18.16b}, v26.16b
	eor		v11.16b, v11.16b, v9.16b
	tbl		v16.16b, {v18.16b}, v27.16b
	zip1		v10.2d, v10.2d, v11.2d
	eor		v12.16b, v12.16b, v19.16b
	eor		v13.16b, v13.16b, v20.16b
	rev64		v19.4s, v21.4s
	eor		v13.16b, v13.16b, v16.16b
	tbl		v16.16b, {v10.16b}, v25.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull		v17.8h, v10.8b, v4.8b
	pmull		v19.8h, v16.8b, v3.8b
	tbl		v21.16b, {v21.16b}, v28.16b
	eor		v12.16b, v12.16b, v23.16b
	eor		v12.16b, v12.16b, v21.16b
	pmull		v14.8h, v10.8b, v3.8b
	eor		v17.16b, v17.16b, v19.16b
	pmull2		v22.8h, v9.16b, v2.16b
	pmull		v21.8h, v10.8b, v5.8b
	tbl		v19.16b, {v17.16b}, v26.16b
	rev64		v11.4s, v10.4s
	.if		\preload
	fmov		d9, x7
	.endif
	eor		v14.16b, v14.16b, v19.16b
	rev64		v20.4s, v22.4s
	.if		\preload
	mov		v9.d[1], x6
	.endif
	rev64		v19.4s, v21.4s
	eor		v11.16b, v11.16b, v10.16b
	eor		v22.16b, v22.16b, v20.16b
	eor		v21.16b, v21.16b, v19.16b
	pmull2		v18.8h, v10.16b, v4.16b
	pmull2		v20.8h, v16.16b, v3.16b
	tbl		v19.16b, {v11.16b}, v25.16b
	pmull2		v15.8h, v10.16b, v3.16b
	zip1		v19.4s, v11.4s, v19.4s
	eor		v18.16b, v18.16b, v20.16b
	tbl		v23.16b, {v17.16b}, v27.16b
	tbl		v20.16b, {v18.16b}, v26.16b
	pmull		v17.8h, v19.8b, v7.8b
	tbl		v16.16b, {v18.16b}, v27.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	eor		v15.16b, v15.16b, v20.16b
	eor		v15.16b, v15.16b, v16.16b
	ext		v18.16b, v17.16b, v17.16b, #8
	eor		v13.16b, v13.16b, v22.16b
	pmull		v16.8h, v11.8b, v6.8b
	pmull2		v22.8h, v10.16b, v5.16b
	eor		v17.16b, v17.16b, v18.16b
	tbl		v19.16b, {v17.16b}, v26.16b
	dup		v18.2d, v16.d[1]
	rev64		v20.4s, v22.4s
	eor		v16.16b, v16.16b, v19.16b
	rev64		v19.4s, v18.4s
	eor		v22.16b, v22.16b, v20.16b
	tbl		v20.16b, {v17.16b}, v27.16b
	eor		v18.16b, v18.16b, v19.16b
	tbl		v18.16b, {v18.16b}, v28.16b
	tbl		v21.16b, {v21.16b}, v28.16b
	tbl		v22.16b, {v22.16b}, v28.16b
	eor		v16.16b, v16.16b, v20.16b
	eor		v14.16b, v14.16b, v23.16b
	eor		v16.16b, v16.16b, v18.16b
	eor		v14.16b, v14.16b, v21.16b
	eor		v15.16b, v15.16b, v22.16b
	ext		v17.16b, v12.16b, v12.16b, #8
	ext		v18.16b, v13.16b, v13.16b, #8
	ext		v19.16b, v15.16b, v15.16b, #8
	ext		v20.16b, v14.16b, v14.16b, #8
	eor		v17.16b, v17.16b, v12.16b
	eor		v18.16b, v18.16b, v13.16b
	eor		v19.16b, v19.16b, v15.16b
	eor		v17.16b, v17.16b, v14.16b
	eor		v18.16b, v18.16b, v20.16b
	eor		v19.16b, v19.16b, v16.16b
	tbl		v17.16b, {v17.16b}, v24.16b
	tbl		v18.16b, {v18.16b}, v24.16b
	tbl		v19.16b, {v19.16b}, v24.16b
	eor		v12.16b, v12.16b, v17.16b
	eor		v13.16b, v13.16b, v18.16b
	eor		v15.16b, v15.16b, v19.16b
	eor		v17.16b, v12.16b, v13.16b
	ext		v18.16b, v12.16b, v13.16b, #8
	eor		v15.16b, v15.16b, v17.16b
	eor		v15.16b, v15.16b, v18.16b
	zip1		v12.2d, v12.2d, v15.2d
	zip2		v13.2d, v15.2d, v13.2d
	pmull		v16.8h, v12.8b, v29.8b
	pmull2		v17.8h, v12.16b, v29.16b
	eor		v13.16b, v13.16b, v12.16b
	uzp1		v18.16b, v16.16b, v17.16b
	uzp2		v19.16b, v16.16b, v17.16b
	ext		v18.16b, v18.16b, v18.16b, #1
	pmull2		v20.8h, v18.16b, v30.16b
	eor		v19.16b, v19.16b, v18.16b
	eor		v13.16b, v13.16b, v19.16b
	eor		v31.16b, v13.16b, v20.16b
	.endm

	.macro		__single_k32_dispatch, mode=0, preload=0
	.if		\mode == 1
	__single_k32_direct \preload
	.elseif		\mode == 2
	__single_k32_packed_scheduled \preload
	.else
	__single_k32_scheduled \preload
	.endif
	.endm

	.macro		__single_k32_input_load
	ld1		{v9.2d}, [x2], #16
CPU_LE(	rev64		v9.16b, v9.16b	)
	ext		v9.16b, v9.16b, v9.16b, #8
	.endm

	.macro		__ghash_single_k32_opt, unroll=1, preload=0, mode=0, stream=0
	cbz		w0, .Lsingle_opt_exit\@
	adr_l		x4, .Lsingle_k32_masks
	ld1		{v24.16b-v27.16b}, [x4]
	ldr		q28, [x4, #64]
CPU_BE(	rev64		v28.16b, v28.16b	)
CPU_BE(	ext		v28.16b, v28.16b, v28.16b, #8	)
	ld1		{v0.2d}, [x3]
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]

	/* Keep all nine key operands and their rotations in registers. */
	rev64		v3.4s, v0.4s
	ext		v6.16b, v0.16b, v0.16b, #8
	eor		v3.16b, v3.16b, v0.16b
	eor		v6.16b, v6.16b, v0.16b
	uzp1		v3.4s, v3.4s, v3.4s
	zip1		v3.2d, v3.2d, v6.2d
	rev64		v6.4s, v3.4s
	eor		v6.16b, v6.16b, v3.16b
	tbl		v1.16b, {v0.16b}, v25.16b
	rev32		v2.8h, v0.8h
	tbl		v4.16b, {v3.16b}, v25.16b
	rev32		v5.8h, v3.8h
	tbl		v7.16b, {v6.16b}, v25.16b
	rev32		v8.8h, v6.8h
	.if		\mode == 2
	zip1		v7.4s, v7.4s, v6.4s
	zip1		v6.4s, v6.4s, v8.4s
	.endif
	.if		\stream
	__single_k32_input_load
	cmp		w0, #(\unroll + 1)
	b.lo		.Lsingle_stream_tail\@
.Lsingle_stream_loop\@:
	.rept		\unroll
	__single_k32_dispatch \mode, 1
	.endr
	sub		w0, w0, #\unroll
	cmp		w0, #(\unroll + 1)
	b.hs		.Lsingle_stream_loop\@
.Lsingle_stream_tail\@:
	cmp		w0, #1
	b.eq		.Lsingle_stream_last\@
.Lsingle_stream_one\@:
	__single_k32_dispatch \mode, 1
	sub		w0, w0, #1
	cmp		w0, #1
	b.hi		.Lsingle_stream_one\@
.Lsingle_stream_last\@:
	__single_k32_dispatch \mode
	.else
	.if		\unroll > 1
	cmp		w0, #\unroll
	b.lo		.Lsingle_opt_tail\@
.Lsingle_opt_loop\@:
	.set		single_opt_i\@, 0
	.rept		\unroll
	.if		single_opt_i\@ == 0 || !\preload
	__single_k32_input_load
	.endif
	__single_k32_dispatch \mode, (\preload && single_opt_i\@ < (\unroll - 1))
	.set		single_opt_i\@, single_opt_i\@ + 1
	.endr
	sub		w0, w0, #\unroll
	cmp		w0, #\unroll
	b.hs		.Lsingle_opt_loop\@
	cbz		w0, .Lsingle_opt_store\@
	.endif
.Lsingle_opt_tail\@:
	__single_k32_input_load
	__single_k32_dispatch \mode
	subs		w0, w0, #1
	b.ne		.Lsingle_opt_tail\@
	.endif
.Lsingle_opt_store\@:
	st1		{v31.2d}, [x1]
.Lsingle_opt_exit\@:
	ret
	.endm

SYM_FUNC_START(ghash_neon_k32_sched_blocks)
	__ghash_single_k32_opt 1
SYM_FUNC_END(ghash_neon_k32_sched_blocks)

SYM_FUNC_START(ghash_neon_k32_u2_blocks)
	__ghash_single_k32_opt 2
SYM_FUNC_END(ghash_neon_k32_u2_blocks)

SYM_FUNC_START(ghash_neon_k32_u4_blocks)
	__ghash_single_k32_opt 4
SYM_FUNC_END(ghash_neon_k32_u4_blocks)

SYM_FUNC_START(ghash_neon_k32_u2_load_blocks)
	__ghash_single_k32_opt 2, 1
SYM_FUNC_END(ghash_neon_k32_u2_load_blocks)

SYM_FUNC_START(ghash_neon_k32_u4_load_blocks)
	__ghash_single_k32_opt 4, 1
SYM_FUNC_END(ghash_neon_k32_u4_load_blocks)

SYM_FUNC_START(ghash_neon_k32_direct_blocks)
	__ghash_single_k32_opt 1, 0, 1
SYM_FUNC_END(ghash_neon_k32_direct_blocks)

SYM_FUNC_START(ghash_neon_k32_direct_u4_load_blocks)
	__ghash_single_k32_opt 4, 1, 1
SYM_FUNC_END(ghash_neon_k32_direct_u4_load_blocks)

SYM_FUNC_START(ghash_neon_k32_packed_sched_blocks)
	__ghash_single_k32_opt 1, 0, 2
SYM_FUNC_END(ghash_neon_k32_packed_sched_blocks)

SYM_FUNC_START(ghash_neon_k32_direct_stream_blocks)
	__ghash_single_k32_opt 4, 1, 1, 1
SYM_FUNC_END(ghash_neon_k32_direct_stream_blocks)

	.section	.rodata, "a"
	.align		4
.Lsingle_k32_masks:
	.byte		255, 255, 255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 255, 255, 255, 255
	.byte		1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
	.byte		255, 0, 1, 2, 3, 4, 5, 255, 255, 8, 9, 10, 11, 12, 13, 255
	.byte		255, 255, 255, 6, 7, 255, 255, 255, 255, 255, 255, 14, 15, 255, 255, 255
	.byte		255, 255, 0, 1, 2, 3, 255, 255, 255, 255, 8, 9, 10, 11, 255, 255

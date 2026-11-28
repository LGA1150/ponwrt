/* SPDX-License-Identifier: GPL-2.0-only */
/* Aggregated GHASH with an additional 32-bit Karatsuba split. */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a

	/* Accumulate two pairs of 32-bit products, kept as byte diagonals. */
	/* pipe: 0 direct, 1 scalar, 2 grouped SIMD, 3 per-vector SIMD. */
	/* 4/5 use scratch stores; 6 schedules scalar transfers individually. */
	/* 7 separates the dependent low/high lane writes with arithmetic. */
	/* 8 retains the current keys without prefetching another group. */
	.macro		__k32_products, ad, p0, p1, p2, add=0, pipe=0
	.if		!\pipe
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
	tbl		v18.16b, {\ad\().16b}, v25.16b
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x6, [x4, #0]
	.endif
	pmull		v19.8h, \ad\().8b, v9.8b
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x7, [x4, #8]
	.endif
	pmull2		v20.8h, \ad\().16b, v9.16b
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x8, [x4, #16]
	.endif
	pmull		v21.8h, \ad\().8b, v10.8b
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x9, [x4, #24]
	.endif
	pmull2		v22.8h, \ad\().16b, v10.16b
	.if		\pipe == 3
	ldr		q10, [x4, #16]
CPU_BE(	ext		v10.16b, v10.16b, v10.16b, #8	)
	.endif
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x10, [x4, #32]
	.endif
	.if		\pipe == 6
	fmov		d10, x8
	mov		v10.d[1], x9
	.endif
	.if		\pipe == 7
	fmov		d10, x8
	.endif
	.if		\add
	eor		v19.16b, v19.16b, v20.16b
	eor		\p0\().16b, \p0\().16b, v19.16b
	.else
	eor		\p0\().16b, v19.16b, v20.16b
	.endif
	.if		\pipe == 4 || \pipe == 5
	stp		x8, x9, [sp, #16]
	.endif
	pmull		v19.8h, v18.8b, v9.8b
	.if		\pipe == 1 || (\pipe >= 4 && \pipe <= 7)
	ldr		x11, [x4, #40]
	.endif
	pmull2		v20.8h, v18.16b, v9.16b
	.if		\pipe == 6
	fmov		d9, x6
	mov		v9.d[1], x7
	.endif
	.if		\pipe == 7
	fmov		d9, x6
	mov		v10.d[1], x9
	.endif
	.if		\pipe == 4 || \pipe == 5
	stp		x6, x7, [sp]
	.endif
	.if		\pipe == 3
	ldr		q9, [x4]
CPU_BE(	ext		v9.16b, v9.16b, v9.16b, #8	)
	.endif
	eor		v21.16b, v21.16b, v22.16b
	.if		\pipe == 4 || \pipe == 5
	stp		x10, x11, [sp, #32]
	.endif
	pmull		v22.8h, \ad\().8b, v11.8b
	.if		\pipe == 7
	mov		v9.d[1], x7
	.endif
	.if		\pipe == 5
	ldr		q10, [sp, #16]
CPU_BE(	ext		v10.16b, v10.16b, v10.16b, #8	)
	.endif
	pmull2		v23.8h, \ad\().16b, v11.16b
	.if		\pipe == 6
	fmov		d11, x10
	mov		v11.d[1], x11
	.endif
	.if		\pipe == 7
	fmov		d11, x10
	.endif
	.if		\pipe == 5
	ldr		q9, [sp]
CPU_BE(	ext		v9.16b, v9.16b, v9.16b, #8	)
	.endif
	.if		\pipe == 3
	ldr		q11, [x4, #32]
CPU_BE(	ext		v11.16b, v11.16b, v11.16b, #8	)
	.endif
	.if		\pipe == 2
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
	eor		v19.16b, v19.16b, v20.16b
	.if		\pipe == 5
	ldr		q11, [sp, #32]
CPU_BE(	ext		v11.16b, v11.16b, v11.16b, #8	)
	.endif
	.if		\add
	eor		v21.16b, v21.16b, v19.16b
	eor		\p1\().16b, \p1\().16b, v21.16b
	eor		v22.16b, v22.16b, v23.16b
	eor		\p2\().16b, \p2\().16b, v22.16b
	.else
	eor		\p1\().16b, v21.16b, v19.16b
	eor		\p2\().16b, v22.16b, v23.16b
	.endif
	.if		\pipe == 1
	fmov		d9, x6
	fmov		d10, x8
	fmov		d11, x10
	mov		v9.d[1], x7
	mov		v10.d[1], x9
	mov		v11.d[1], x11
	.endif
	.if		\pipe == 4
	ld1		{v9.2d-v11.2d}, [sp]
	.endif
	.if		\pipe == 7
	mov		v11.d[1], x11
	.endif
	.if		\pipe == 1 || (\pipe >= 3 && \pipe <= 7)
	add		x4, x4, #48
	.endif
	.endm

	/* Each 64-bit lane becomes one complete 32x32 product sum. */
	.macro		__k32_combine, p0, p1, p2
	tbl		v18.16b, {\p1\().16b}, v26.16b
	tbl		v19.16b, {\p1\().16b}, v27.16b
	eor		\p0\().16b, \p0\().16b, v18.16b
	ushr		v18.2d, \p2\().2d, #32
	eor		\p0\().16b, \p0\().16b, v19.16b
	eor		\p2\().16b, \p2\().16b, v18.16b
	shl		\p2\().2d, \p2\().2d, #32
	ushr		\p2\().2d, \p2\().2d, #16
	eor		\p0\().16b, \p0\().16b, \p2\().16b
	.endm

	.macro		__k32_reconstruct, rq, sched=0, acc=0, no64=0
	.if		\sched
	tbl		v18.16b, {v13.16b}, v26.16b
	tbl		v19.16b, {v16.16b}, v26.16b
	tbl		v20.16b, {v13.16b}, v27.16b
	tbl		v21.16b, {v16.16b}, v27.16b
	eor		v12.16b, v12.16b, v18.16b
	eor		v15.16b, v15.16b, v19.16b
	.if		\sched == 2
	rev64		v18.4s, v14.4s
	rev64		v19.4s, v17.4s
	.else
	ushr		v18.2d, v14.2d, #32
	ushr		v19.2d, v17.2d, #32
	.endif
	eor		v12.16b, v12.16b, v20.16b
	eor		v15.16b, v15.16b, v21.16b
	eor		v14.16b, v14.16b, v18.16b
	eor		v17.16b, v17.16b, v19.16b
	.if		\sched == 2
	tbl		v14.16b, {v14.16b}, v30.16b
	tbl		v17.16b, {v17.16b}, v30.16b
	.else
	shl		v14.2d, v14.2d, #32
	shl		v17.2d, v17.2d, #32
	ushr		v14.2d, v14.2d, #16
	ushr		v17.2d, v17.2d, #16
	.endif
	eor		v12.16b, v12.16b, v14.16b
	eor		v15.16b, v15.16b, v17.16b
	.else
	__k32_combine	v12, v13, v14
	__k32_combine	v15, v16, v17
	.endif
	.if		\no64
	ext		v19.16b, v15.16b, v15.16b, #8
	eor		v18.16b, v15.16b, v19.16b
	.else
	eor		v18.16b, v12.16b, v15.16b
	ext		v19.16b, v18.16b, v18.16b, #8
	eor		v18.16b, v18.16b, v19.16b
	.endif
	.if		\sched == 2
	tbl		v18.16b, {v18.16b}, v28.16b
	.else
	mov		v18.8b, v18.8b
	ext		v18.16b, v28.16b, v18.16b, #12
	.endif
	.if		\acc
	eor		\rq\().16b, \rq\().16b, v12.16b
	eor		\rq\().16b, \rq\().16b, v18.16b
	.else
	eor		\rq\().16b, v12.16b, v18.16b
	.endif
	.endm

	/* After input consumes the digest, v31 can retain the low inner cross. */
	.macro		__k32_cross, a, b, reuse=0
	.if		\reuse == 3
	eor		v24.16b, v24.16b, v31.16b
	.else
	rev64		v18.4s, \a\().4s
	rev64		v19.4s, \b\().4s
	eor		v18.16b, v18.16b, \a\().16b
	eor		v19.16b, v19.16b, \b\().16b
	.if		\reuse == 1
	uzp1		v31.4s, v18.4s, v19.4s
	.else
	uzp1		v24.4s, v18.4s, v19.4s
	.endif
	.endif
	.endm

	/* Four 64x64 products: low/high 32-bit products and their cross terms. */
	.macro		__k32_part, rq, a, b, pipe=0, last=0, sched=0, acc=0, wrap=256, \
			reuse=0, early=0
	.if		\early
	__k32_cross	\a, \b, \reuse
	.endif
	__k32_products	\a, v12, v13, v14, 0, \pipe
	__k32_products	\b, v12, v13, v14, 1, \pipe
	.if		!\early
	__k32_cross	\a, \b, \reuse
	.endif
	.if		\pipe && \last
	add		x4, x3, #\wrap
	.endif
	.if		\reuse == 1
	__k32_products	v31, v15, v16, v17, 0, \pipe
	.else
	__k32_products	v24, v15, v16, v17, 0, \pipe
	.endif
	.if		\last && \early == 2
	shl		v23.2d, v29.2d, #56
	.endif
	__k32_reconstruct \rq, \sched, \acc
	.endm

	.macro		__k32_input, carry=1, compact=0
	.if		\compact == 2 && \carry
	ext		v31.16b, v31.16b, v31.16b, #8
	.endif
	ld1		{v18.2d-v21.2d}, [x2], #64
CPU_LE(	rev64		v18.16b, v18.16b	)
CPU_LE(	rev64		v19.16b, v19.16b	)
CPU_LE(	rev64		v20.16b, v20.16b	)
CPU_LE(	rev64		v21.16b, v21.16b	)
	.if		\compact == 1 && \carry
	ext		v31.16b, v31.16b, v31.16b, #8
	.endif
	.if		!\compact
	ext		v18.16b, v18.16b, v18.16b, #8
	ext		v19.16b, v19.16b, v19.16b, #8
	ext		v20.16b, v20.16b, v20.16b, #8
	ext		v21.16b, v21.16b, v21.16b, #8
	.endif
	.if		\carry
	eor		v18.16b, v18.16b, v31.16b
	.endif
	.if		\compact
	zip2		v0.2d, v18.2d, v19.2d
	zip1		v1.2d, v18.2d, v19.2d
	zip2		v2.2d, v20.2d, v21.2d
	zip1		v3.2d, v20.2d, v21.2d
	.else
	zip1		v0.2d, v18.2d, v19.2d
	zip2		v1.2d, v18.2d, v19.2d
	zip1		v2.2d, v20.2d, v21.2d
	zip2		v3.2d, v20.2d, v21.2d
	.endif
	eor		v4.16b, v0.16b, v1.16b
	eor		v5.16b, v2.16b, v3.16b
	.endm

	.macro		__k32_reduce, sched=0, no128=0, ready=0
	.if		!\no128
	eor		v18.16b, v6.16b, v7.16b
	.endif
	ext		v19.16b, v6.16b, v7.16b, #8
	.if		!\no128
	eor		v8.16b, v8.16b, v18.16b
	.endif
	eor		v8.16b, v8.16b, v19.16b
	zip1		v6.2d, v6.2d, v8.2d
	zip2		v7.2d, v8.2d, v7.2d
	pmull		v18.8h, v6.8b, v29.8b
	pmull2		v19.8h, v6.16b, v29.16b
	eor		v7.16b, v7.16b, v6.16b
	uzp1		v20.16b, v18.16b, v19.16b
	uzp2		v21.16b, v18.16b, v19.16b
	ext		v20.16b, v20.16b, v20.16b, #1
	.if		\sched == 2
	.if		!\ready
	shl		v23.2d, v29.2d, #56
	.endif
	pmull2		v22.8h, v20.16b, v23.16b
	.else
	pmull2		v22.8h, v20.16b, v30.16b
	.endif
	eor		v21.16b, v21.16b, v20.16b
	eor		v7.16b, v7.16b, v21.16b
	eor		v31.16b, v7.16b, v22.16b
	.endm

	.macro		__ghash_k32, pipe=0, sched=0, compact=0, reuse=0, early=0
	cmp		w0, #4
	b.lo		ghash_neon_2x_load_blocks
	.if		\pipe == 4 || \pipe == 5
	sub		sp, sp, #48
	.endif
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	.if		\sched == 2
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	.else
	ld1		{v25.16b-v27.16b}, [x4]
	movi		v28.16b, #0
	.endif
	movi		v29.16b, #0xc2
	.if		\sched != 2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]
	.endif
	.if		\pipe
	add		x4, x3, #256
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
1:
	__k32_input	1, \compact
	.if		!\pipe
	add		x4, x3, #256
	.endif
	__k32_part	v6, v0, v2, \pipe, 0, \sched, 0, 256, \reuse, \early
	__k32_part	v7, v1, v3, \pipe, 0, \sched, 0, 256, (2 * \reuse), \early
	__k32_part	v8, v4, v5, \pipe, 1, \sched, 0, 256, (3 * \reuse), \early

	__k32_reduce	\sched, 0, (\early == 2)
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		1b
	st1		{v31.2d}, [x1]
	.if		\pipe == 4 || \pipe == 5
	stp		xzr, xzr, [sp]
	stp		xzr, xzr, [sp, #16]
	stp		xzr, xzr, [sp, #32]
	add		sp, sp, #48
	.endif
	cbnz		w0, ghash_neon_2x_load_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_4x_k32_blocks)
	__ghash_k32
SYM_FUNC_END(ghash_neon_4x_k32_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_load_blocks)
	__ghash_k32 1
SYM_FUNC_END(ghash_neon_4x_k32_load_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_sched_blocks)
	__ghash_k32 1, 1
SYM_FUNC_END(ghash_neon_4x_k32_sched_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tbl_blocks)
	__ghash_k32 1, 2
SYM_FUNC_END(ghash_neon_4x_k32_tbl_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tbl_simd_blocks)
	__ghash_k32 0, 2
SYM_FUNC_END(ghash_neon_4x_k32_tbl_simd_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tbl_simd_pipe_blocks)
	__ghash_k32 2, 2
SYM_FUNC_END(ghash_neon_4x_k32_tbl_simd_pipe_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tbl_simd_ldr_blocks)
	__ghash_k32 3, 2
SYM_FUNC_END(ghash_neon_4x_k32_tbl_simd_ldr_blocks)

	.macro		__ghash_k32_8, pipe=1
	cmp		w0, #8
	b.lo		ghash_neon_4x_k32_tbl_blocks
	.if		\pipe == 4 || \pipe == 5
	sub		sp, sp, #48
	.endif
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	movi		v29.16b, #0xc2
	add		x4, x3, #688
	ld1		{v9.2d-v11.2d}, [x4], #48
1:
	__k32_input
	__k32_part	v6, v0, v2, \pipe, 0, 2
	__k32_part	v7, v1, v3, \pipe, 0, 2
	__k32_part	v8, v4, v5, \pipe, 1, 2

	__k32_input	0
	__k32_part	v6, v0, v2, \pipe, 0, 2, 1
	__k32_part	v7, v1, v3, \pipe, 0, 2, 1
	__k32_part	v8, v4, v5, \pipe, 1, 2, 1, 688
	__k32_reduce	2
	sub		w0, w0, #8
	cmp		w0, #8
	b.hs		1b
	st1		{v31.2d}, [x1]
	.if		\pipe == 4 || \pipe == 5
	stp		xzr, xzr, [sp]
	stp		xzr, xzr, [sp, #16]
	stp		xzr, xzr, [sp, #32]
	add		sp, sp, #48
	.endif
	cbnz		w0, ghash_neon_4x_k32_tbl_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_8x_k32_tbl_blocks)
	__ghash_k32_8
SYM_FUNC_END(ghash_neon_8x_k32_tbl_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_stp_blocks)
	__ghash_k32 4, 2
SYM_FUNC_END(ghash_neon_4x_k32_stp_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_stp_pipe_blocks)
	__ghash_k32 5, 2
SYM_FUNC_END(ghash_neon_4x_k32_stp_pipe_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_stp_blocks)
	__ghash_k32_8 4
SYM_FUNC_END(ghash_neon_8x_k32_stp_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_stp_pipe_blocks)
	__ghash_k32_8 5
SYM_FUNC_END(ghash_neon_8x_k32_stp_pipe_blocks)

	/* v12-v17 retain byte diagonals across two groups before reconstruction. */
	.macro		__k32_raw4, a, b, add=0, pipe=1, next=256
	__k32_products	\a, v12, v13, v14, \add, \pipe
	__k32_products	\b, v12, v13, v14, 1, \pipe
	rev64		v18.4s, \a\().4s
	rev64		v19.4s, \b\().4s
	eor		v18.16b, v18.16b, \a\().16b
	eor		v19.16b, v19.16b, \b\().16b
	uzp1		v24.4s, v18.4s, v19.4s
	.if		\pipe
	add		x4, x3, #\next
	.endif
	__k32_products	v24, v15, v16, v17, \add, \pipe
	.if		!\pipe
	add		x4, x3, #\next
	.endif
	.endm

	.macro		__k32_part8, rq, a, b, c, d, pipe, part, next, base=256, acc=0
	__k32_raw4	\a, \b, 0, \pipe, (\base + 144 * \part)
	__k32_raw4	\c, \d, 1, \pipe, \next
	__k32_reconstruct \rq, 2, \acc
	.endm

	.macro		__k32_input_half, lo0, hi0, lo1, hi1, carry=0
	ld1		{v18.2d-v21.2d}, [x2], #64
CPU_LE(	rev64		v18.16b, v18.16b	)
CPU_LE(	rev64		v19.16b, v19.16b	)
CPU_LE(	rev64		v20.16b, v20.16b	)
CPU_LE(	rev64		v21.16b, v21.16b	)
	ext		v18.16b, v18.16b, v18.16b, #8
	ext		v19.16b, v19.16b, v19.16b, #8
	ext		v20.16b, v20.16b, v20.16b, #8
	ext		v21.16b, v21.16b, v21.16b, #8
	.if		\carry
	eor		v18.16b, v18.16b, v31.16b
	.endif
	zip1		\lo0\().2d, v18.2d, v19.2d
	zip2		\hi0\().2d, v18.2d, v19.2d
	zip1		\lo1\().2d, v20.2d, v21.2d
	zip2		\hi1\().2d, v20.2d, v21.2d
	.endm

	.macro		__ghash_k32_merge, pipe=1
	cmp		w0, #8
	b.lo		ghash_neon_4x_k32_tbl_blocks
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	add		x4, x3, #688
	.if		\pipe
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
1:
	__k32_input_half v0, v4, v1, v5, 1
	__k32_input_half v2, v29, v3, v31
	__k32_part8	v6, v0, v1, v2, v3, \pipe, 0, 832
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	__k32_part8	v7, v4, v5, v29, v31, \pipe, 1, 976
	__k32_part8	v8, v0, v1, v2, v3, \pipe, 2, 688
	movi		v29.16b, #0xc2
	__k32_reduce	2
	sub		w0, w0, #8
	cmp		w0, #8
	b.hs		1b
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_4x_k32_tbl_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_8x_k32_merge_blocks)
	__ghash_k32_merge
SYM_FUNC_END(ghash_neon_8x_k32_merge_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_merge_simd_blocks)
	__ghash_k32_merge 0
SYM_FUNC_END(ghash_neon_8x_k32_merge_simd_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_merge_ldr_blocks)
	__ghash_k32_merge 3
SYM_FUNC_END(ghash_neon_8x_k32_merge_ldr_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_merge_early_blocks)
	__ghash_k32_merge 6
SYM_FUNC_END(ghash_neon_8x_k32_merge_early_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_early_blocks)
	__ghash_k32_8 6
SYM_FUNC_END(ghash_neon_8x_k32_early_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_merge_lanes_blocks)
	__ghash_k32_merge 7
SYM_FUNC_END(ghash_neon_8x_k32_merge_lanes_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_lanes_blocks)
	__ghash_k32_8 7
SYM_FUNC_END(ghash_neon_8x_k32_lanes_blocks)

	/* Reuse each key for the direct low/high and swapped 32-bit products. */
	.macro		__k32_raw4_no64, a, b, add=0, next=256, early=0
	.if		\early
	rev64		v24.4s, \a\().4s
	.endif
	__k32_products	\a, v12, v13, v14, \add, 8
	.if		!\early
	rev64		v24.4s, \a\().4s
	.endif
	__k32_products	v24, v15, v16, v17, \add, 1
	.if		\early
	rev64		v24.4s, \b\().4s
	.endif
	__k32_products	\b, v12, v13, v14, 1, 8
	.if		!\early
	rev64		v24.4s, \b\().4s
	.endif
	add		x4, x3, #\next
	__k32_products	v24, v15, v16, v17, 1, 1
	.endm

	.macro		__k32_less_raw, a, b, add, next, no64=0, early=0
	.if		\no64
	__k32_raw4_no64	\a, \b, \add, \next, \early
	.else
	__k32_raw4	\a, \b, \add, 1, \next
	.endif
	.endm

	.macro		__k32_less_part, rq, a, b, c, d, part, next, no64=0, early=0
	__k32_less_raw	\a, \b, 0, (256 + 144 * \part), \no64, \early
	__k32_less_raw	\c, \d, 1, \next, \no64, \early
	__k32_reconstruct \rq, 2, 0, \no64
	.endm

	.macro		__ghash_k32_less, no128=0, no64=0, early=0
	cmp		w0, #8
	b.lo		ghash_neon_4x_k32_tbl_blocks
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	add		x4, x3, #688
	ld1		{v9.2d-v11.2d}, [x4], #48
1:
	__k32_input_half v0, v4, v1, v5, 1
	__k32_input_half v2, v29, v3, v31
	__k32_less_part	v6, v0, v1, v2, v3, 0, 832, \no64, \early
	.if		\no128
	__k32_less_part	v7, v4, v5, v29, v31, 1, 832, \no64, \early
	__k32_less_raw	v0, v1, 0, 400, \no64, \early
	__k32_less_raw	v2, v3, 1, 688, \no64, \early
	__k32_less_raw	v4, v5, 1, 256, \no64, \early
	__k32_less_raw	v29, v31, 1, 688, \no64, \early
	__k32_reconstruct v8, 2, 0, \no64
	.else
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	__k32_less_part	v7, v4, v5, v29, v31, 1, 976, \no64, \early
	__k32_less_part	v8, v0, v1, v2, v3, 2, 688, \no64, \early
	.endif
	movi		v29.16b, #0xc2
	__k32_reduce	2, \no128
	sub		w0, w0, #8
	cmp		w0, #8
	b.hs		1b
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_4x_k32_tbl_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_8x_k32_no128_blocks)
	__ghash_k32_less 1, 0
SYM_FUNC_END(ghash_neon_8x_k32_no128_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_no64_blocks)
	__ghash_k32_less 0, 1
SYM_FUNC_END(ghash_neon_8x_k32_no64_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_no128_no64_blocks)
	__ghash_k32_less 1, 1
SYM_FUNC_END(ghash_neon_8x_k32_no128_no64_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_no64_early_blocks)
	__ghash_k32_less 0, 1, 1
SYM_FUNC_END(ghash_neon_8x_k32_no64_early_blocks)

SYM_FUNC_START(ghash_neon_8x_k32_no128_no64_early_blocks)
	__ghash_k32_less 1, 1, 1
SYM_FUNC_END(ghash_neon_8x_k32_no128_no64_early_blocks)

	/* Share the final field reduction across two eight-block product sums. */
SYM_FUNC_START(ghash_neon_16x_k32_blocks)
	cmp		w0, #16
	b.lo		ghash_neon_8x_k32_merge_blocks
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	add		x4, x3, #1552
	ld1		{v9.2d-v11.2d}, [x4], #48
1:
	__k32_input_half v0, v4, v1, v5, 1
	__k32_input_half v2, v29, v3, v31
	__k32_part8	v6, v0, v1, v2, v3, 1, 0, 1696, 1120
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	__k32_part8	v7, v4, v5, v29, v31, 1, 1, 1840, 1120
	__k32_part8	v8, v0, v1, v2, v3, 1, 2, 688, 1120

	__k32_input_half v0, v4, v1, v5
	__k32_input_half v2, v29, v3, v31
	__k32_part8	v6, v0, v1, v2, v3, 1, 0, 832, 256, 1
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	__k32_part8	v7, v4, v5, v29, v31, 1, 1, 976, 256, 1
	__k32_part8	v8, v0, v1, v2, v3, 1, 2, 1552, 256, 1
	movi		v29.16b, #0xc2
	__k32_reduce	2
	sub		w0, w0, #16
	cmp		w0, #16
	b.hs		1b
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_8x_k32_merge_blocks
	ret
SYM_FUNC_END(ghash_neon_16x_k32_blocks)

	/* Retain the first half's high/cross inputs while summing 16 blocks. */
SYM_FUNC_START(ghash_neon_16x_k32_merge_blocks)
	cmp		w0, #16
	b.lo		ghash_neon_8x_k32_merge_blocks
	sub		sp, sp, #128
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	add		x4, x3, #1552
	ld1		{v9.2d-v11.2d}, [x4], #48
1:
	__k32_input_half v0, v4, v1, v5, 1
	__k32_input_half v2, v29, v3, v31
	__k32_raw4	v0, v1, 0, 1, 1120
	__k32_raw4	v2, v3, 1, 1, 688
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	stp		q4, q5, [sp]
	stp		q29, q31, [sp, #32]
	stp		q0, q1, [sp, #64]
	stp		q2, q3, [sp, #96]

	__k32_input_half v0, v4, v1, v5
	__k32_input_half v2, v29, v3, v31
	__k32_raw4	v0, v1, 1, 1, 256
	__k32_raw4	v2, v3, 1, 1, 832
	__k32_reconstruct v6, 2
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v29.16b
	eor		v3.16b, v3.16b, v31.16b
	__k32_raw4	v4, v5, 0, 1, 400
	__k32_raw4	v29, v31, 1, 1, 1696
	ldp		q4, q5, [sp]
	ldp		q29, q31, [sp, #32]
	__k32_raw4	v4, v5, 1, 1, 1264
	__k32_raw4	v29, v31, 1, 1, 976
	__k32_reconstruct v7, 2

	__k32_raw4	v0, v1, 0, 1, 544
	__k32_raw4	v2, v3, 1, 1, 1840
	ldp		q0, q1, [sp, #64]
	ldp		q2, q3, [sp, #96]
	__k32_raw4	v0, v1, 1, 1, 1408
	__k32_raw4	v2, v3, 1, 1, 1552
	__k32_reconstruct v8, 2
	movi		v29.16b, #0xc2
	__k32_reduce	2
	sub		w0, w0, #16
	cmp		w0, #16
	b.hs		1b
	st1		{v31.2d}, [x1]
	movi		v0.16b, #0
	stp		q0, q0, [sp]
	stp		q0, q0, [sp, #32]
	stp		q0, q0, [sp, #64]
	stp		q0, q0, [sp, #96]
	add		sp, sp, #128
	cbnz		w0, ghash_neon_8x_k32_merge_blocks
	ret
SYM_FUNC_END(ghash_neon_16x_k32_merge_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_input_blocks)
	__ghash_k32 1, 2, 1
SYM_FUNC_END(ghash_neon_4x_k32_input_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_cross_blocks)
	__ghash_k32 1, 2, 0, 1
SYM_FUNC_END(ghash_neon_4x_k32_cross_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tight_blocks)
	__ghash_k32 1, 2, 1, 1
SYM_FUNC_END(ghash_neon_4x_k32_tight_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_tight_early_blocks)
	__ghash_k32 1, 2, 1, 1, 1
SYM_FUNC_END(ghash_neon_4x_k32_tight_early_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_pipe_blocks)
	__ghash_k32 1, 2, 2, 1, 2
SYM_FUNC_END(ghash_neon_4x_k32_pipe_blocks)

	/* Build the three rotated forms of one group of four 32-bit keys. */
	.macro		__k32_compact_store, a
	.ifnc		\a, v10
	mov		v10.16b, \a\().16b
	.endif
	tbl		v11.16b, {v10.16b}, v25.16b
	rev32		v12.8h, v10.8h
	st1		{v10.2d-v12.2d}, [x17], #48
	.endm

	.macro		__k32_compact_part, a, b
	__k32_compact_store \a
	__k32_compact_store \b
	rev64		v10.4s, \a\().4s
	rev64		v11.4s, \b\().4s
	eor		v10.16b, v10.16b, \a\().16b
	eor		v11.16b, v11.16b, \b\().16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_compact_store v10
	.endm

SYM_FUNC_START(ghash_neon_4x_k32_compact_blocks)
	/* The persistent layout is raw H followed by reflected H^1..H^4. */
	add		x3, x3, #16
	cmp		w0, #4
	b.lo		ghash_neon_k32_direct_stream_blocks
	mov		x16, x3
	sub		sp, sp, #432
	mov		x17, sp

	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	ldp		q0, q1, [x3, #32]
	ldp		q2, q3, [x3]
CPU_BE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_BE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
	zip1		v4.2d, v1.2d, v0.2d
	zip2		v5.2d, v1.2d, v0.2d
	zip1		v6.2d, v3.2d, v2.2d
	zip2		v7.2d, v3.2d, v2.2d
	eor		v8.16b, v4.16b, v5.16b
	eor		v9.16b, v6.16b, v7.16b
	__k32_compact_part v4, v6
	__k32_compact_part v5, v7
	__k32_compact_part v8, v9

	/* Reuse the tight four-block loop with a transient operand table. */
	mov		x3, sp
	mov		x4, sp
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	ld1		{v9.2d-v11.2d}, [x4], #48
.Lcompact4_loop:
	__k32_input	1, 1
	__k32_part	v6, v0, v2, 1, 0, 2, 0, 0, 1, 1
	__k32_part	v7, v1, v3, 1, 0, 2, 0, 0, 2, 1
	__k32_part	v8, v4, v5, 1, 1, 2, 0, 0, 3, 1
	__k32_reduce	2
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		.Lcompact4_loop
	st1		{v31.2d}, [x1]

	.irp		off, 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, \
			192, 208, 224, 240, 256, 272, 288, 304, 320, 336, 352, \
			368, 384, 400, 416
	stp		xzr, xzr, [sp, #\off]
	.endr
	add		sp, sp, #432
	mov		x3, x16
	cbnz		w0, ghash_neon_k32_direct_stream_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_k32_compact_blocks)

	/* Generate rotations from fewer saved operands while retiring a group. */
	.macro		__k32_small_products, ad, p0, p1, p2, add=0, mode=144, \
			lo=x6, hi=x7, cross=0, lo2=x4, hi2=x5
	tbl		v18.16b, {\ad\().16b}, v25.16b
	.if		\mode == 144 || \mode == 288
	ldr		x6, [x4]
	.elseif		\cross
	eor		x16, \lo, \lo, lsr #32
	.endif
	pmull		v19.8h, \ad\().8b, v9.8b
	.if		\mode == 144 || \mode == 288
	ldr		x7, [x4, #8]
	.elseif		\cross
	eor		x17, \hi, \hi, lsr #32
	.endif
	pmull2		v20.8h, \ad\().16b, v9.16b
	.if		\mode == 288
	ldr		x8, [x4, #16]
	.endif
	pmull		v21.8h, \ad\().8b, v10.8b
	.if		\mode == 288
	ldr		x9, [x4, #24]
	.endif
	pmull2		v22.8h, \ad\().16b, v10.16b
	.if		\mode == 288
	fmov		d10, x8
	.endif
	.if		\add
	eor		v19.16b, v19.16b, v20.16b
	eor		\p0\().16b, \p0\().16b, v19.16b
	.else
	eor		\p0\().16b, v19.16b, v20.16b
	.endif
	pmull		v19.8h, v18.8b, v9.8b
	.if		\mode == 288
	mov		v10.d[1], x9
	.endif
	pmull2		v20.8h, v18.16b, v9.16b
	.if		\cross
	fmov		s9, w16
	eor		x16, \lo2, \lo2, lsr #32
	.else
	fmov		d9, \lo
	.endif
	eor		v21.16b, v21.16b, v22.16b
	.if		\cross
	mov		v9.s[1], w17
	eor		x17, \hi2, \hi2, lsr #32
	mov		v9.s[2], w16
	.else
	mov		v9.d[1], \hi
	.endif
	pmull		v22.8h, \ad\().8b, v11.8b
	.if		\cross
	mov		v9.s[3], w17
	.elseif		\mode != 288
	tbl		v10.16b, {v9.16b}, v25.16b
	.endif
	pmull2		v23.8h, \ad\().16b, v11.16b
	.if		\cross
	tbl		v10.16b, {v9.16b}, v25.16b
	.endif
	rev32		v11.8h, v9.8h
	eor		v19.16b, v19.16b, v20.16b
	.if		\add
	eor		v21.16b, v21.16b, v19.16b
	eor		\p1\().16b, \p1\().16b, v21.16b
	eor		v22.16b, v22.16b, v23.16b
	eor		\p2\().16b, \p2\().16b, v22.16b
	.else
	eor		\p1\().16b, v21.16b, v19.16b
	eor		\p2\().16b, v22.16b, v23.16b
	.endif
	.if		\mode == 144
	add		x4, x4, #16
	.elseif		\mode == 288
	add		x4, x4, #32
	.endif
	.endm

	.macro		__k32_small_store, a, mode, lo=x4, hi=x5
	.ifnc		\a, v10
	mov		v10.16b, \a\().16b
	.endif
	.if		\mode == 288
	tbl		v11.16b, {v10.16b}, v25.16b
	st1		{v10.2d-v11.2d}, [x17], #32
	.elseif		\mode == 144
	st1		{v10.2d}, [x17], #16
	.else
	umov		\lo, v10.d[0]
	umov		\hi, v10.d[1]
	.endif
	.endm

SYM_FUNC_START(ghash_neon_4x_k32_stack288_blocks)
	add		x3, x3, #16
	cmp		w0, #4
	b.lo		ghash_neon_k32_direct_stream_blocks
	sub		sp, sp, #288
	mov		x17, sp
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	ldp		q0, q1, [x3, #32]
	ldp		q2, q3, [x3]
CPU_BE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_BE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
	zip1		v4.2d, v1.2d, v0.2d
	zip2		v5.2d, v1.2d, v0.2d
	zip1		v6.2d, v3.2d, v2.2d
	zip2		v7.2d, v3.2d, v2.2d
	eor		v8.16b, v4.16b, v5.16b
	eor		v9.16b, v6.16b, v7.16b
	__k32_small_store v4, 288, x4, x5
	__k32_small_store v6, 288, x6, x7
	rev64		v10.4s, v4.4s
	rev64		v11.4s, v6.4s
	eor		v10.16b, v10.16b, v4.16b
	eor		v11.16b, v11.16b, v6.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 288, x8, x9
	__k32_small_store v5, 288, x10, x11
	__k32_small_store v7, 288, x12, x13
	rev64		v10.4s, v5.4s
	rev64		v11.4s, v7.4s
	eor		v10.16b, v10.16b, v5.16b
	eor		v11.16b, v11.16b, v7.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 288, x14, x15
	__k32_small_store v8, 288, x16, x17
	__k32_small_store v9, 288, x19, x20
	rev64		v10.4s, v8.4s
	rev64		v11.4s, v9.4s
	eor		v10.16b, v10.16b, v8.16b
	eor		v11.16b, v11.16b, v9.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 288, x21, x22
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	mov		x4, sp
	ld1		{v9.2d-v10.2d}, [x4], #32
	rev32		v11.8h, v9.8h
.Lstack288_loop:
	__k32_input	1, 1
	__k32_cross	v0, v2, 1
	__k32_small_products v0, v12, v13, v14, 0, 288, x6, x7
	__k32_small_products v2, v12, v13, v14, 1, 288, x6, x7
	__k32_small_products v31, v15, v16, v17, 0, 288, x6, x7
	__k32_reconstruct v6, 2
	__k32_cross	v1, v3, 2
	__k32_small_products v1, v12, v13, v14, 0, 288, x6, x7
	__k32_small_products v3, v12, v13, v14, 1, 288, x6, x7
	__k32_small_products v24, v15, v16, v17, 0, 288, x6, x7
	__k32_reconstruct v7, 2
	__k32_cross	v4, v5, 3
	__k32_small_products v4, v12, v13, v14, 0, 288, x6, x7
	__k32_small_products v5, v12, v13, v14, 1, 288, x6, x7
	mov		x4, sp
	__k32_small_products v24, v15, v16, v17, 0, 288, x6, x7
	__k32_reconstruct v8, 2
	__k32_reduce	2
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		.Lstack288_loop
	st1		{v31.2d}, [x1]
	.irp		off, 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, \
			192, 208, 224, 240, 256, 272
	stp		xzr, xzr, [sp, #\off]
	.endr
	add		sp, sp, #288
	cbnz		w0, ghash_neon_k32_direct_stream_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_k32_stack288_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_stack144_blocks)
	add		x3, x3, #16
	cmp		w0, #4
	b.lo		ghash_neon_k32_direct_stream_blocks
	sub		sp, sp, #144
	mov		x17, sp
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	ldp		q0, q1, [x3, #32]
	ldp		q2, q3, [x3]
CPU_BE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_BE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
	zip1		v4.2d, v1.2d, v0.2d
	zip2		v5.2d, v1.2d, v0.2d
	zip1		v6.2d, v3.2d, v2.2d
	zip2		v7.2d, v3.2d, v2.2d
	eor		v8.16b, v4.16b, v5.16b
	eor		v9.16b, v6.16b, v7.16b
	__k32_small_store v4, 144, x4, x5
	__k32_small_store v6, 144, x6, x7
	rev64		v10.4s, v4.4s
	rev64		v11.4s, v6.4s
	eor		v10.16b, v10.16b, v4.16b
	eor		v11.16b, v11.16b, v6.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 144, x8, x9
	__k32_small_store v5, 144, x10, x11
	__k32_small_store v7, 144, x12, x13
	rev64		v10.4s, v5.4s
	rev64		v11.4s, v7.4s
	eor		v10.16b, v10.16b, v5.16b
	eor		v11.16b, v11.16b, v7.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 144, x14, x15
	__k32_small_store v8, 144, x16, x17
	__k32_small_store v9, 144, x19, x20
	rev64		v10.4s, v8.4s
	rev64		v11.4s, v9.4s
	eor		v10.16b, v10.16b, v8.16b
	eor		v11.16b, v11.16b, v9.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 144, x21, x22
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	mov		x4, sp
	ld1		{v9.2d}, [x4], #16
	tbl		v10.16b, {v9.16b}, v25.16b
	rev32		v11.8h, v9.8h
.Lstack144_loop:
	__k32_input	1, 1
	__k32_cross	v0, v2, 1
	__k32_small_products v0, v12, v13, v14, 0, 144, x6, x7
	__k32_small_products v2, v12, v13, v14, 1, 144, x6, x7
	__k32_small_products v31, v15, v16, v17, 0, 144, x6, x7
	__k32_reconstruct v6, 2
	__k32_cross	v1, v3, 2
	__k32_small_products v1, v12, v13, v14, 0, 144, x6, x7
	__k32_small_products v3, v12, v13, v14, 1, 144, x6, x7
	__k32_small_products v24, v15, v16, v17, 0, 144, x6, x7
	__k32_reconstruct v7, 2
	__k32_cross	v4, v5, 3
	__k32_small_products v4, v12, v13, v14, 0, 144, x6, x7
	__k32_small_products v5, v12, v13, v14, 1, 144, x6, x7
	mov		x4, sp
	__k32_small_products v24, v15, v16, v17, 0, 144, x6, x7
	__k32_reconstruct v8, 2
	__k32_reduce	2
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		.Lstack144_loop
	st1		{v31.2d}, [x1]
	.irp		off, 0, 16, 32, 48, 64, 80, 96, 112, 128
	stp		xzr, xzr, [sp, #\off]
	.endr
	add		sp, sp, #144
	cbnz		w0, ghash_neon_k32_direct_stream_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_k32_stack144_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_regs32_blocks)
	add		x3, x3, #16
	cmp		w0, #4
	b.lo		ghash_neon_k32_direct_stream_blocks
	stp		x19, x20, [sp, #-32]!
	stp		x21, x22, [sp, #16]
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	ldp		q0, q1, [x3, #32]
	ldp		q2, q3, [x3]
CPU_BE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_BE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
	zip1		v4.2d, v1.2d, v0.2d
	zip2		v5.2d, v1.2d, v0.2d
	zip1		v6.2d, v3.2d, v2.2d
	zip2		v7.2d, v3.2d, v2.2d
	eor		v8.16b, v4.16b, v5.16b
	eor		v9.16b, v6.16b, v7.16b
	__k32_small_store v4, 32, x4, x5
	__k32_small_store v6, 32, x6, x7
	rev64		v10.4s, v4.4s
	rev64		v11.4s, v6.4s
	eor		v10.16b, v10.16b, v4.16b
	eor		v11.16b, v11.16b, v6.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 32, x8, x9
	__k32_small_store v5, 32, x10, x11
	__k32_small_store v7, 32, x12, x13
	rev64		v10.4s, v5.4s
	rev64		v11.4s, v7.4s
	eor		v10.16b, v10.16b, v5.16b
	eor		v11.16b, v11.16b, v7.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 32, x14, x15
	__k32_small_store v8, 32, x16, x17
	__k32_small_store v9, 32, x19, x20
	rev64		v10.4s, v8.4s
	rev64		v11.4s, v9.4s
	eor		v10.16b, v10.16b, v8.16b
	eor		v11.16b, v11.16b, v9.16b
	uzp1		v10.4s, v10.4s, v11.4s
	__k32_small_store v10, 32, x21, x22
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	fmov		d9, x4
	mov		v9.d[1], x5
	tbl		v10.16b, {v9.16b}, v25.16b
	rev32		v11.8h, v9.8h
.Lregs32_loop:
	__k32_input	1, 1
	__k32_cross	v0, v2, 1
	__k32_small_products v0, v12, v13, v14, 0, 32, x6, x7
	__k32_small_products v2, v12, v13, v14, 1, 32, x8, x9
	__k32_small_products v31, v15, v16, v17, 0, 32, x10, x11
	__k32_reconstruct v6, 2
	__k32_cross	v1, v3, 2
	__k32_small_products v1, v12, v13, v14, 0, 32, x12, x13
	__k32_small_products v3, v12, v13, v14, 1, 32, x14, x15
	__k32_small_products v24, v15, v16, v17, 0, 32, x16, x17
	__k32_reconstruct v7, 2
	__k32_cross	v4, v5, 3
	__k32_small_products v4, v12, v13, v14, 0, 32, x19, x20
	__k32_small_products v5, v12, v13, v14, 1, 32, x21, x22
	__k32_small_products v24, v15, v16, v17, 0, 32, x4, x5
	__k32_reconstruct v8, 2
	__k32_reduce	2
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		.Lregs32_loop
	st1		{v31.2d}, [x1]
	ldp		x21, x22, [sp, #16]
	ldp		x19, x20, [sp], #32
	cbnz		w0, ghash_neon_k32_direct_stream_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_k32_regs32_blocks)

SYM_FUNC_START(ghash_neon_4x_k32_regs0_blocks)
	add		x3, x3, #16
	cmp		w0, #4
	b.lo		ghash_neon_k32_direct_stream_blocks
	adr_l		x4, .Lk32_masks
	ld1		{v25.16b-v28.16b}, [x4], #64
	ld1		{v30.16b}, [x4]
	ldp		q0, q1, [x3, #32]
	ldp		q2, q3, [x3]
CPU_BE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_BE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_BE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_BE(	ext		v3.16b, v3.16b, v3.16b, #8	)
	zip1		v4.2d, v1.2d, v0.2d
	zip2		v5.2d, v1.2d, v0.2d
	zip1		v6.2d, v3.2d, v2.2d
	zip2		v7.2d, v3.2d, v2.2d
	eor		v8.16b, v4.16b, v5.16b
	eor		v9.16b, v6.16b, v7.16b
	__k32_small_store v4, 0, x4, x5
	__k32_small_store v6, 0, x6, x7
	__k32_small_store v5, 0, x8, x9
	__k32_small_store v7, 0, x10, x11
	__k32_small_store v8, 0, x12, x13
	__k32_small_store v9, 0, x14, x15
	ld1		{v31.2d}, [x1]
	movi		v29.16b, #0xc2
	fmov		d9, x4
	mov		v9.d[1], x5
	tbl		v10.16b, {v9.16b}, v25.16b
	rev32		v11.8h, v9.8h
.Lregs0_loop:
	__k32_input	1, 1
	__k32_cross	v0, v2, 1
	__k32_small_products v0, v12, v13, v14, 0, 0, x6, x7
	__k32_small_products v2, v12, v13, v14, 1, 0, x4, x5, 1, x6, x7
	__k32_small_products v31, v15, v16, v17, 0, 0, x8, x9
	__k32_reconstruct v6, 2
	__k32_cross	v1, v3, 2
	__k32_small_products v1, v12, v13, v14, 0, 0, x10, x11
	__k32_small_products v3, v12, v13, v14, 1, 0, x8, x9, 1, x10, x11
	__k32_small_products v24, v15, v16, v17, 0, 0, x12, x13
	__k32_reconstruct v7, 2
	__k32_cross	v4, v5, 3
	__k32_small_products v4, v12, v13, v14, 0, 0, x14, x15
	__k32_small_products v5, v12, v13, v14, 1, 0, x12, x13, 1, x14, x15
	__k32_small_products v24, v15, v16, v17, 0, 0, x4, x5
	__k32_reconstruct v8, 2
	__k32_reduce	2
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		.Lregs0_loop
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_k32_direct_stream_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_k32_regs0_blocks)

	.section	.rodata, "a"
	.align		4
.Lk32_masks:
	.byte		1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
	.byte		255, 0, 1, 2, 3, 4, 5, 255, 255, 8, 9, 10, 11, 12, 13, 255
	.byte		255, 255, 255, 6, 7, 255, 255, 255, 255, 255, 255, 14, 15, 255, 255, 255
	.byte		255, 255, 255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 255, 255, 255, 255
	.byte		255, 255, 0, 1, 2, 3, 255, 255, 255, 255, 8, 9, 10, 11, 255, 255

/* SPDX-License-Identifier: GPL-2.0-only */
/* Direct 128-bit polynomial products using byte PMULL. */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a

	/* v19 is the input, v20:v21 accumulate the low:high product halves. */
	.macro		__schoolbook_diagonal, shift, key, mask
	ext		v22.16b, v19.16b, v19.16b, #\shift
	pmull		v25.8h, v19.8b, \key\().8b
	pmull		v23.8h, v22.8b, v0.8b
	pmull2		v26.8h, v19.16b, \key\().16b
	pmull2		v24.8h, v22.16b, v0.16b
	eor		v25.16b, v25.16b, v23.16b
	eor		v26.16b, v26.16b, v24.16b
	/* Move wrapped byte products down by 128 bits before placement. */
	and		v27.16b, v26.16b, \mask\().16b
	eor		v25.16b, v25.16b, v27.16b
	eor		v26.16b, v26.16b, v27.16b
	ext		v28.16b, v18.16b, v25.16b, #(16 - \shift)
	ext		v29.16b, v25.16b, v26.16b, #(16 - \shift)
	eor		v20.16b, v20.16b, v28.16b
	eor		v21.16b, v21.16b, v29.16b
	.endm

SYM_FUNC_START(ghash_neon_schoolbook_blocks)
	ld1		{v0.2d}, [x3]
	ld1		{v19.2d}, [x1]
	ext		v1.16b, v0.16b, v0.16b, #1
	ext		v2.16b, v0.16b, v0.16b, #2
	ext		v3.16b, v0.16b, v0.16b, #3
	ext		v4.16b, v0.16b, v0.16b, #4
	ext		v5.16b, v0.16b, v0.16b, #5
	ext		v6.16b, v0.16b, v0.16b, #6
	ext		v7.16b, v0.16b, v0.16b, #7
	ext		v8.16b, v0.16b, v0.16b, #8
	adr_l		x4, .Lschoolbook_masks
	ld1		{v9.16b-v12.16b}, [x4], #64
	ld1		{v13.16b-v15.16b}, [x4]
	movi		v16.16b, #0xc2
	movi		v17.16b, #0
	mov		v17.b[15], v16.b[0]
	movi		v18.16b, #0

.Lschoolbook_loop:
	ld1		{v22.2d}, [x2], #16
	sub		w0, w0, #1
CPU_LE(	rev64		v22.16b, v22.16b	)
	ext		v22.16b, v22.16b, v22.16b, #8
	eor		v19.16b, v19.16b, v22.16b

	pmull		v20.8h, v19.8b, v0.8b
	pmull2		v21.8h, v19.16b, v0.16b
	__schoolbook_diagonal 1, v1, v9
	__schoolbook_diagonal 2, v2, v10
	__schoolbook_diagonal 3, v3, v11
	__schoolbook_diagonal 4, v4, v12
	__schoolbook_diagonal 5, v5, v13
	__schoolbook_diagonal 6, v6, v14
	__schoolbook_diagonal 7, v7, v15

	/* The distance-eight products occupy the middle 128 product bits. */
	pmull		v25.8h, v19.8b, v8.8b
	pmull2		v26.8h, v19.16b, v8.16b
	eor		v25.16b, v25.16b, v26.16b
	ext		v28.16b, v18.16b, v25.16b, #8
	ext		v29.16b, v25.16b, v18.16b, #8
	eor		v20.16b, v20.16b, v28.16b
	eor		v21.16b, v21.16b, v29.16b

	pmull		v23.8h, v20.8b, v16.8b
	pmull2		v24.8h, v20.16b, v16.16b
	eor		v21.16b, v21.16b, v20.16b
	uzp1		v25.16b, v23.16b, v24.16b
	uzp2		v26.16b, v23.16b, v24.16b
	ext		v25.16b, v25.16b, v25.16b, #1
	pmull2		v27.8h, v25.16b, v17.16b
	eor		v26.16b, v26.16b, v25.16b
	eor		v21.16b, v21.16b, v26.16b
	eor		v19.16b, v21.16b, v27.16b
	cbnz		w0, .Lschoolbook_loop

	st1		{v19.2d}, [x1]
	ret
SYM_FUNC_END(ghash_neon_schoolbook_blocks)

	/* Sum one diagonal from four products before correcting its wrap. */
	.macro		__schoolbook_4x_term, shift, input, key, rotated, first=0, \
			next=0, offset=0, low=d8
	ext		v22.16b, \input\().16b, \input\().16b, #\shift
	.if		\next
	ldr		x6, [x4, #\offset]
	.endif
	pmull		v25.8h, \input\().8b, \rotated\().8b
	.if		\next
	ldr		x7, [x4, #(\offset + 8)]
	.endif
	pmull		v27.8h, v22.8b, \key\().8b
	pmull2		v26.8h, \input\().16b, \rotated\().16b
	pmull2		v28.8h, v22.16b, \key\().16b
	.if		\next
	fmov		\low, x6
	.endif
	.if		\first
	eor		v23.16b, v25.16b, v27.16b
	eor		v24.16b, v26.16b, v28.16b
	.else
	eor		v25.16b, v25.16b, v27.16b
	eor		v26.16b, v26.16b, v28.16b
	eor		v23.16b, v23.16b, v25.16b
	eor		v24.16b, v24.16b, v26.16b
	.endif
	.if		\next
	mov		\rotated\().d[1], x7
	.endif
	.endm

	.macro		__schoolbook_4x_diagonal, shift, mask, load=1, reverse=0, next=0
	.if		\load
	.if		\reverse
	ld1		{v8.2d-v11.2d}, [x4], x5
	.else
	ld1		{v8.2d-v11.2d}, [x4], #64
	.endif
	.endif
	__schoolbook_4x_term \shift, v0, v4, v8, 1, \next, 0, d8
	__schoolbook_4x_term \shift, v1, v5, v9, 0, \next, 16, d9
	__schoolbook_4x_term \shift, v2, v6, v10, 0, \next, 32, d10
	__schoolbook_4x_term \shift, v3, v7, v11, 0, \next, 48, d11
	and		v25.16b, v24.16b, \mask\().16b
	.if		\next
	.if		\reverse
	add		x4, x4, x5
	.else
	add		x4, x4, #64
	.endif
	.endif
	eor		v23.16b, v23.16b, v25.16b
	eor		v24.16b, v24.16b, v25.16b
	ext		v27.16b, v19.16b, v23.16b, #(16 - \shift)
	ext		v28.16b, v23.16b, v24.16b, #(16 - \shift)
	eor		v20.16b, v20.16b, v27.16b
	eor		v21.16b, v21.16b, v28.16b
	.endm

SYM_FUNC_START(ghash_neon_4x_schoolbook_blocks)
	cmp		w0, #4
	b.lo		ghash_neon_schoolbook_blocks
	ld1		{v31.2d}, [x1]
	/* H^4, H^3, H^2, H stay in v4-v7 across all four-block groups. */
	add		x4, x3, #16
	ld1		{v4.2d-v7.2d}, [x4]
	adr_l		x4, .Lschoolbook_masks
	ld1		{v12.16b-v15.16b}, [x4], #64
	ld1		{v16.16b-v18.16b}, [x4]
	movi		v19.16b, #0
	movi		v29.16b, #0xc2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]

.Lschoolbook_4x_loop:
	ld1		{v0.2d-v3.2d}, [x2], #64
	sub		w0, w0, #4
CPU_LE(	rev64		v0.16b, v0.16b	)
CPU_LE(	rev64		v1.16b, v1.16b	)
CPU_LE(	rev64		v2.16b, v2.16b	)
CPU_LE(	rev64		v3.16b, v3.16b	)
	ext		v0.16b, v0.16b, v0.16b, #8
	ext		v1.16b, v1.16b, v1.16b, #8
	ext		v2.16b, v2.16b, v2.16b, #8
	ext		v3.16b, v3.16b, v3.16b, #8
	eor		v0.16b, v0.16b, v31.16b
	add		x4, x3, #80

	pmull		v20.8h, v0.8b, v4.8b
	pmull2		v21.8h, v0.16b, v4.16b
	pmull		v23.8h, v1.8b, v5.8b
	pmull2		v24.8h, v1.16b, v5.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	pmull		v23.8h, v2.8b, v6.8b
	pmull2		v24.8h, v2.16b, v6.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	pmull		v23.8h, v3.8b, v7.8b
	pmull2		v24.8h, v3.16b, v7.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	__schoolbook_4x_diagonal 1, v12
	__schoolbook_4x_diagonal 2, v13
	__schoolbook_4x_diagonal 3, v14
	__schoolbook_4x_diagonal 4, v15
	__schoolbook_4x_diagonal 5, v16
	__schoolbook_4x_diagonal 6, v17
	__schoolbook_4x_diagonal 7, v18

	ld1		{v8.2d-v11.2d}, [x4]
	pmull		v23.8h, v0.8b, v8.8b
	pmull2		v24.8h, v0.16b, v8.16b
	eor		v23.16b, v23.16b, v24.16b
	pmull		v25.8h, v1.8b, v9.8b
	pmull2		v26.8h, v1.16b, v9.16b
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	pmull		v25.8h, v2.8b, v10.8b
	pmull2		v26.8h, v2.16b, v10.16b
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	pmull		v25.8h, v3.8b, v11.8b
	pmull2		v26.8h, v3.16b, v11.16b
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	ext		v27.16b, v19.16b, v23.16b, #8
	ext		v28.16b, v23.16b, v19.16b, #8
	eor		v20.16b, v20.16b, v27.16b
	eor		v21.16b, v21.16b, v28.16b

	/* Reduce the sum of all four complete 256-bit products once. */
	pmull		v23.8h, v20.8b, v29.8b
	pmull2		v24.8h, v20.16b, v29.16b
	eor		v21.16b, v21.16b, v20.16b
	uzp1		v25.16b, v23.16b, v24.16b
	uzp2		v26.16b, v23.16b, v24.16b
	ext		v25.16b, v25.16b, v25.16b, #1
	pmull2		v27.8h, v25.16b, v30.16b
	eor		v26.16b, v26.16b, v25.16b
	eor		v21.16b, v21.16b, v26.16b
	eor		v31.16b, v21.16b, v27.16b
	cmp		w0, #4
	b.hs		.Lschoolbook_4x_loop

	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_schoolbook_blocks
	ret
SYM_FUNC_END(ghash_neon_4x_schoolbook_blocks)

	.macro		__schoolbook_4x_zero, first=0
	.if		\first
	pmull		v20.8h, v0.8b, v4.8b
	pmull2		v21.8h, v0.16b, v4.16b
	.else
	pmull		v23.8h, v0.8b, v4.8b
	pmull2		v24.8h, v0.16b, v4.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	.endif
	pmull		v23.8h, v1.8b, v5.8b
	pmull2		v24.8h, v1.16b, v5.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	pmull		v23.8h, v2.8b, v6.8b
	pmull2		v24.8h, v2.16b, v6.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	pmull		v23.8h, v3.8b, v7.8b
	pmull2		v24.8h, v3.16b, v7.16b
	eor		v20.16b, v20.16b, v23.16b
	eor		v21.16b, v21.16b, v24.16b
	.endm

	.macro		__schoolbook_4x_middle, first=0, load=1, next=0
	.if		\load
	ld1		{v8.2d-v11.2d}, [x4], #64
	.endif
	.if		\next
	ldr		x6, [x4]
	.endif
	pmull		v23.8h, v0.8b, v8.8b
	.if		\next
	ldr		x7, [x4, #8]
	.endif
	pmull2		v24.8h, v0.16b, v8.16b
	.if		\next
	fmov		d8, x6
	.endif
	eor		v23.16b, v23.16b, v24.16b
	.if		\next
	mov		v8.d[1], x7
	ldr		x6, [x4, #16]
	.endif
	pmull		v25.8h, v1.8b, v9.8b
	.if		\next
	ldr		x7, [x4, #24]
	.endif
	pmull2		v26.8h, v1.16b, v9.16b
	.if		\next
	fmov		d9, x6
	.endif
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	.if		\next
	mov		v9.d[1], x7
	ldr		x6, [x4, #32]
	.endif
	pmull		v25.8h, v2.8b, v10.8b
	.if		\next
	ldr		x7, [x4, #40]
	.endif
	pmull2		v26.8h, v2.16b, v10.16b
	.if		\next
	fmov		d10, x6
	.endif
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	.if		\next
	mov		v10.d[1], x7
	ldr		x6, [x4, #48]
	.endif
	pmull		v25.8h, v3.8b, v11.8b
	.if		\next
	ldr		x7, [x4, #56]
	.endif
	pmull2		v26.8h, v3.16b, v11.16b
	.if		\next
	fmov		d11, x6
	.endif
	eor		v25.16b, v25.16b, v26.16b
	eor		v23.16b, v23.16b, v25.16b
	.if		\next
	mov		v11.d[1], x7
	add		x4, x4, x5
	.endif
	.if		\first
	ext		v20.16b, v19.16b, v23.16b, #8
	ext		v21.16b, v23.16b, v19.16b, #8
	.else
	ext		v27.16b, v19.16b, v23.16b, #8
	ext		v28.16b, v23.16b, v19.16b, #8
	eor		v20.16b, v20.16b, v27.16b
	eor		v21.16b, v21.16b, v28.16b
	.endif
	.endm

	.macro		__schoolbook_4x_walk_step, reverse=0, pipe_key=0
	ld1		{v0.2d-v3.2d}, [x2], #64
	sub		w0, w0, #4
CPU_LE(	rev64		v0.16b, v0.16b	)
CPU_LE(	rev64		v1.16b, v1.16b	)
CPU_LE(	rev64		v2.16b, v2.16b	)
CPU_LE(	rev64		v3.16b, v3.16b	)
	ext		v0.16b, v0.16b, v0.16b, #8
	ext		v1.16b, v1.16b, v1.16b, #8
	ext		v2.16b, v2.16b, v2.16b, #8
	ext		v3.16b, v3.16b, v3.16b, #8
	eor		v0.16b, v0.16b, v31.16b
	.if		\reverse
	/* Rotation eight is cached; x4 is just beyond its table. */
	sub		x4, x4, #128
	.if		\pipe_key
	__schoolbook_4x_middle 1, 0, 1
	__schoolbook_4x_diagonal 7, v18, 0, 1, 1
	__schoolbook_4x_diagonal 6, v17, 0, 1, 1
	__schoolbook_4x_diagonal 5, v16, 0, 1, 1
	__schoolbook_4x_diagonal 4, v15, 0, 1, 1
	__schoolbook_4x_diagonal 3, v14, 0, 1, 1
	__schoolbook_4x_diagonal 2, v13, 0, 1, 1
	__schoolbook_4x_diagonal 1, v12, 0
	.else
	__schoolbook_4x_middle 1, 0
	__schoolbook_4x_diagonal 7, v18, 1, 1
	__schoolbook_4x_diagonal 6, v17, 1, 1
	__schoolbook_4x_diagonal 5, v16, 1, 1
	__schoolbook_4x_diagonal 4, v15, 1, 1
	__schoolbook_4x_diagonal 3, v14, 1, 1
	__schoolbook_4x_diagonal 2, v13, 1, 1
	__schoolbook_4x_diagonal 1, v12, 1, 1
	.endif
	__schoolbook_4x_zero
	/* Rotation one survives reduction; next forward load is two. */
	add		x4, x4, #128
	.else
	__schoolbook_4x_zero 1
	.if		\pipe_key
	__schoolbook_4x_diagonal 1, v12, 0, 0, 1
	__schoolbook_4x_diagonal 2, v13, 0, 0, 1
	__schoolbook_4x_diagonal 3, v14, 0, 0, 1
	__schoolbook_4x_diagonal 4, v15, 0, 0, 1
	__schoolbook_4x_diagonal 5, v16, 0, 0, 1
	__schoolbook_4x_diagonal 6, v17, 0, 0, 1
	__schoolbook_4x_diagonal 7, v18, 0, 0, 1
	__schoolbook_4x_middle 0, 0
	.else
	__schoolbook_4x_diagonal 1, v12, 0
	__schoolbook_4x_diagonal 2, v13
	__schoolbook_4x_diagonal 3, v14
	__schoolbook_4x_diagonal 4, v15
	__schoolbook_4x_diagonal 5, v16
	__schoolbook_4x_diagonal 6, v17
	__schoolbook_4x_diagonal 7, v18
	__schoolbook_4x_middle
	.endif
	.endif
	pmull		v23.8h, v20.8b, v29.8b
	pmull2		v24.8h, v20.16b, v29.16b
	eor		v21.16b, v21.16b, v20.16b
	uzp1		v25.16b, v23.16b, v24.16b
	uzp2		v26.16b, v23.16b, v24.16b
	ext		v25.16b, v25.16b, v25.16b, #1
	pmull2		v27.8h, v25.16b, v30.16b
	eor		v26.16b, v26.16b, v25.16b
	eor		v21.16b, v21.16b, v26.16b
	eor		v31.16b, v21.16b, v27.16b
	cmp		w0, #4
	.endm

	.macro		__schoolbook_4x_walk, pipe_key=0
	cmp		w0, #4
	b.lo		ghash_neon_schoolbook_blocks
	ld1		{v31.2d}, [x1]
	add		x4, x3, #16
	ld1		{v4.2d-v7.2d}, [x4]
	adr_l		x4, .Lschoolbook_masks
	ld1		{v12.16b-v15.16b}, [x4], #64
	ld1		{v16.16b-v18.16b}, [x4]
	movi		v19.16b, #0
	movi		v29.16b, #0xc2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]
	mov		x5, #-64
	add		x4, x3, #80
	ld1		{v8.2d-v11.2d}, [x4], #64
1:
	__schoolbook_4x_walk_step 0, \pipe_key
	b.lo		2f
	__schoolbook_4x_walk_step 1, \pipe_key
	b.hs		1b
2:
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_schoolbook_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_4x_schoolbook_reuse_blocks)
	__schoolbook_4x_walk
SYM_FUNC_END(ghash_neon_4x_schoolbook_reuse_blocks)

SYM_FUNC_START(ghash_neon_4x_schoolbook_load_blocks)
	__schoolbook_4x_walk 1
SYM_FUNC_END(ghash_neon_4x_schoolbook_load_blocks)

	.section	".rodata", "a"
	.align		4
.Lschoolbook_masks:
	.byte		0, 0, 0, 0, 0, 0, 0, 0
	.byte		0, 0, 0, 0, 0, 0, 255, 255
	.byte		0, 0, 0, 0, 0, 0, 0, 0
	.byte		0, 0, 0, 0, 255, 255, 255, 255
	.byte		0, 0, 0, 0, 0, 0, 0, 0
	.byte		0, 0, 255, 255, 255, 255, 255, 255
	.byte		0, 0, 0, 0, 0, 0, 0, 0
	.byte		255, 255, 255, 255, 255, 255, 255, 255
	.byte		0, 0, 0, 0, 0, 0, 255, 255
	.byte		255, 255, 255, 255, 255, 255, 255, 255
	.byte		0, 0, 0, 0, 255, 255, 255, 255
	.byte		255, 255, 255, 255, 255, 255, 255, 255
	.byte		0, 0, 255, 255, 255, 255, 255, 255
	.byte		255, 255, 255, 255, 255, 255, 255, 255

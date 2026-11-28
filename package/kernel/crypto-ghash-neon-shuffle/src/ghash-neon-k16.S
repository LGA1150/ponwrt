/* SPDX-License-Identifier: GPL-2.0-only */
/* Four-block GHASH with Karatsuba splits down to 16-bit or byte operands. */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a

	/* Raw byte products for four 32-bit products; fold paired operands. */
	.macro		__k16_products, ad, p0, p1, p2, p3, add=0, pipe=0, byte=0
	.if		!\pipe
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
	.if		\byte
	rev16		v24.16b, \ad\().16b
	.else
	rev32		v24.8h, \ad\().8h
	.endif
	.if		\pipe
	ldr		x6, [x4]
	.endif
	pmull		v20.8h, \ad\().8b, v9.8b
	.if		\pipe
	ldr		x7, [x4, #8]
	.endif
	pmull2		v21.8h, \ad\().16b, v9.16b
	.if		\pipe
	ldr		x8, [x4, #16]
	.endif
	eor		v24.16b, v24.16b, \ad\().16b
	.if		\pipe
	ldr		x9, [x4, #24]
	.endif
	.if		\byte
	uzp1		v24.16b, v24.16b, v24.16b
	.if		\pipe
	ldr		x10, [x4, #32]
	.endif
	.if		\add
	pmull		v22.8h, v24.8b, v10.8b
	.else
	pmull		\p1\().8h, v24.8b, v10.8b
	.endif
	.else
	pmull		v22.8h, \ad\().8b, v10.8b
	.if		\pipe
	ldr		x10, [x4, #32]
	.endif
	pmull2		v23.8h, \ad\().16b, v10.16b
	.endif
	.if		\pipe
	ldr		x11, [x4, #40]
	.endif
	.if		\add
	eor		v20.16b, v20.16b, v21.16b
	eor		\p0\().16b, \p0\().16b, v20.16b
	.else
	eor		\p0\().16b, v20.16b, v21.16b
	.endif
	.if		\add
	.if		!\byte
	eor		v22.16b, v22.16b, v23.16b
	.endif
	eor		\p1\().16b, \p1\().16b, v22.16b
	.elseif		!\byte
	eor		\p1\().16b, v22.16b, v23.16b
	.endif
	.if		\byte
	rev32		v24.8h, \ad\().8h
	eor		v24.16b, v24.16b, \ad\().16b
	.endif
	uzp1		v24.8h, v24.8h, v24.8h
	.if		\add
	pmull		v20.8h, v24.8b, v11.8b
	.else
	pmull		\p2\().8h, v24.8b, v11.8b
	.endif
	.if		\byte
	rev16		v21.16b, v24.16b
	eor		v24.16b, v24.16b, v21.16b
	uzp1		v24.16b, v24.16b, v24.16b
	.endif
	.if		\add
	pmull2		v21.8h, v24.16b, v11.16b
	eor		\p2\().16b, \p2\().16b, v20.16b
	eor		\p3\().16b, \p3\().16b, v21.16b
	.else
	pmull2		\p3\().8h, v24.16b, v11.16b
	.endif
	.if		\pipe
	fmov		d9, x6
	fmov		d10, x8
	fmov		d11, x10
	mov		v9.d[1], x7
	mov		v10.d[1], x9
	mov		v11.d[1], x11
	add		x4, x4, #48
	.endif
	.endm

	/* Reconstruct both sets of 32-bit products with independent chains. */
	.macro		__k16_combine, byte=0
	.if		\byte
	ext		v20.16b, v13.16b, v13.16b, #8
	ext		v21.16b, v17.16b, v17.16b, #8
	zip1		v15.8h, v15.8h, v15.8h
	zip1		v19.8h, v19.8h, v19.8h
	eor		v13.16b, v13.16b, v20.16b
	eor		v17.16b, v17.16b, v21.16b
	zip1		v13.8h, v13.8h, v13.8h
	zip1		v17.8h, v17.8h, v17.8h
	rev32		v20.8h, v12.8h
	rev32		v21.8h, v16.8h
	rev32		v22.8h, v14.8h
	rev32		v23.8h, v18.8h
	eor		v13.16b, v13.16b, v12.16b
	eor		v17.16b, v17.16b, v16.16b
	eor		v15.16b, v15.16b, v14.16b
	eor		v19.16b, v19.16b, v18.16b
	eor		v13.16b, v13.16b, v20.16b
	eor		v17.16b, v17.16b, v21.16b
	eor		v15.16b, v15.16b, v22.16b
	eor		v19.16b, v19.16b, v23.16b
	.else
	rev32		v20.8h, v13.8h
	rev32		v21.8h, v17.8h
	rev32		v22.8h, v15.8h
	rev32		v23.8h, v19.8h
	eor		v13.16b, v13.16b, v20.16b
	eor		v17.16b, v17.16b, v21.16b
	eor		v15.16b, v15.16b, v22.16b
	eor		v19.16b, v19.16b, v23.16b
	.endif
	tbl		v13.16b, {v13.16b}, v25.16b
	tbl		v17.16b, {v17.16b}, v25.16b
	tbl		v15.16b, {v15.16b}, v25.16b
	tbl		v19.16b, {v19.16b}, v25.16b
	eor		v12.16b, v12.16b, v13.16b
	eor		v16.16b, v16.16b, v17.16b
	eor		v14.16b, v14.16b, v15.16b
	eor		v18.16b, v18.16b, v19.16b
	rev64		v20.4s, v12.4s
	rev64		v21.4s, v16.4s
	ext		v22.16b, v14.16b, v14.16b, #8
	ext		v23.16b, v18.16b, v18.16b, #8
	eor		v20.16b, v20.16b, v12.16b
	eor		v21.16b, v21.16b, v16.16b
	eor		v14.16b, v14.16b, v22.16b
	eor		v18.16b, v18.16b, v23.16b
	zip1		v14.4s, v14.4s, v14.4s
	zip1		v18.4s, v18.4s, v18.4s
	eor		v14.16b, v14.16b, v20.16b
	eor		v18.16b, v18.16b, v21.16b
	tbl		v14.16b, {v14.16b}, v26.16b
	tbl		v18.16b, {v18.16b}, v26.16b
	eor		v12.16b, v12.16b, v14.16b
	eor		v16.16b, v16.16b, v18.16b
	.endm

	.macro		__k16_part, rq, a, b, pipe=0, last=0, byte=0
	__k16_products	\a, v12, v13, v14, v15, 0, \pipe, \byte
	__k16_products	\b, v12, v13, v14, v15, 1, \pipe, \byte
	rev64		v20.4s, \a\().4s
	rev64		v21.4s, \b\().4s
	eor		v20.16b, v20.16b, \a\().16b
	eor		v21.16b, v21.16b, \b\().16b
	uzp1		v28.4s, v20.4s, v21.4s
	.if		\pipe && \last
	add		x4, x3, #256
	.endif
	__k16_products	v28, v16, v17, v18, v19, 0, \pipe, \byte
	__k16_combine	\byte
	eor		v20.16b, v12.16b, v16.16b
	ext		v21.16b, v20.16b, v20.16b, #8
	eor		v20.16b, v20.16b, v21.16b
	tbl		v20.16b, {v20.16b}, v27.16b
	eor		\rq\().16b, v12.16b, v20.16b
	.endm

	.macro		__ghash_k16, pipe=0, byte=0
	cmp		w0, #4
	b.lo		ghash_neon_2x_load_blocks
	ld1		{v31.2d}, [x1]
	adr_l		x4, .Lk16_masks
	ld1		{v25.16b-v27.16b}, [x4]
	movi		v29.16b, #0xc2
	movi		v30.16b, #0
	mov		v30.b[15], v29.b[0]
	.if		\pipe
	add		x4, x3, #256
	ld1		{v9.2d-v11.2d}, [x4], #48
	.endif
1:
	ld1		{v20.2d-v23.2d}, [x2], #64
CPU_LE(	rev64		v20.16b, v20.16b	)
CPU_LE(	rev64		v21.16b, v21.16b	)
CPU_LE(	rev64		v22.16b, v22.16b	)
CPU_LE(	rev64		v23.16b, v23.16b	)
	ext		v20.16b, v20.16b, v20.16b, #8
	ext		v21.16b, v21.16b, v21.16b, #8
	ext		v22.16b, v22.16b, v22.16b, #8
	ext		v23.16b, v23.16b, v23.16b, #8
	eor		v20.16b, v20.16b, v31.16b
	zip1		v0.2d, v20.2d, v21.2d
	zip2		v1.2d, v20.2d, v21.2d
	zip1		v2.2d, v22.2d, v23.2d
	zip2		v3.2d, v22.2d, v23.2d
	eor		v4.16b, v0.16b, v1.16b
	eor		v5.16b, v2.16b, v3.16b
	.if		!\pipe
	add		x4, x3, #256
	.endif
	__k16_part	v6, v0, v2, \pipe, 0, \byte
	__k16_part	v7, v1, v3, \pipe, 0, \byte
	__k16_part	v8, v4, v5, \pipe, 1, \byte
	eor		v20.16b, v6.16b, v7.16b
	ext		v21.16b, v6.16b, v7.16b, #8
	eor		v8.16b, v8.16b, v20.16b
	eor		v8.16b, v8.16b, v21.16b
	zip1		v6.2d, v6.2d, v8.2d
	zip2		v7.2d, v8.2d, v7.2d
	pmull		v20.8h, v6.8b, v29.8b
	pmull2		v21.8h, v6.16b, v29.16b
	eor		v7.16b, v7.16b, v6.16b
	uzp1		v22.16b, v20.16b, v21.16b
	uzp2		v23.16b, v20.16b, v21.16b
	ext		v22.16b, v22.16b, v22.16b, #1
	pmull2		v24.8h, v22.16b, v30.16b
	eor		v23.16b, v23.16b, v22.16b
	eor		v7.16b, v7.16b, v23.16b
	eor		v31.16b, v7.16b, v24.16b
	sub		w0, w0, #4
	cmp		w0, #4
	b.hs		1b
	st1		{v31.2d}, [x1]
	cbnz		w0, ghash_neon_2x_load_blocks
	ret
	.endm

SYM_FUNC_START(ghash_neon_4x_k16_blocks)
	__ghash_k16
SYM_FUNC_END(ghash_neon_4x_k16_blocks)

SYM_FUNC_START(ghash_neon_4x_k16_load_blocks)
	__ghash_k16 1
SYM_FUNC_END(ghash_neon_4x_k16_load_blocks)

SYM_FUNC_START(ghash_neon_4x_k8_blocks)
	__ghash_k16 0, 1
SYM_FUNC_END(ghash_neon_4x_k8_blocks)

SYM_FUNC_START(ghash_neon_4x_k8_load_blocks)
	__ghash_k16 1, 1
SYM_FUNC_END(ghash_neon_4x_k8_load_blocks)

	.section	.rodata, "a"
	.align		4
.Lk16_masks:
	.byte		255, 0, 1, 255, 255, 4, 5, 255, 255, 8, 9, 255, 255, 12, 13, 255
	.byte		255, 255, 0, 1, 2, 3, 255, 255, 255, 255, 8, 9, 10, 11, 255, 255
	.byte		255, 255, 255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 255, 255, 255, 255

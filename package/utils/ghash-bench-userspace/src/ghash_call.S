/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>

	.arch armv8-a
	.text

	.macro save_neon, offset
	stp	d8, d9, [sp, #\offset]
	stp	d10, d11, [sp, #(\offset + 16)]
	stp	d12, d13, [sp, #(\offset + 32)]
	stp	d14, d15, [sp, #(\offset + 48)]
	.endm

	.macro restore_neon, offset
	ldp	d8, d9, [sp, #\offset]
	ldp	d10, d11, [sp, #(\offset + 16)]
	ldp	d12, d13, [sp, #(\offset + 32)]
	ldp	d14, d15, [sp, #(\offset + 48)]
	.endm

SYM_FUNC_START(ghash_user_blocks)
	cbz	w1, 1f
	stp	x29, x30, [sp, #-80]!
	mov	x29, sp
	save_neon 16
	mov	x16, x0
	mov	w0, w1
	mov	x1, x2
	mov	x2, x3
	mov	x3, x4
	blr	x16
	restore_neon 16
	ldp	x29, x30, [sp], #80
1:	ret
SYM_FUNC_END(ghash_user_blocks)

	.macro save_all
	stp	x29, x30, [sp, #-128]!
	mov	x29, sp
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	stp	x23, x24, [sp, #48]
	save_neon 64
	.endm

	.macro restore_all
	restore_neon 64
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x23, x24, [sp, #48]
	ldp	x29, x30, [sp], #128
	.endm

/* Reset the digest for each call, but preserve the ABI once per batch. */
SYM_FUNC_START(ghash_user_repeat)
	cbz	x5, 2f
	cbnz	w1, 3f
	stp	xzr, xzr, [x2]
	ret
3:	save_all
	mov	x19, x0
	mov	w20, w1
	mov	x21, x2
	mov	x22, x3
	mov	x23, x4
	mov	x24, x5
1:	stp	xzr, xzr, [x21]
	mov	w0, w20
	mov	x1, x21
	mov	x2, x22
	mov	x3, x23
	blr	x19
	subs	x24, x24, #1
	b.ne	1b
	restore_all
2:	ret
SYM_FUNC_END(ghash_user_repeat)

/* Check preservation around both public entry points, outside timing. */
SYM_FUNC_START(ghash_user_abi_check)
	save_all
	sub	sp, sp, #48
	stp	x0, x1, [sp]
	stp	x2, x3, [sp, #16]
	str	x4, [sp, #32]
	.irp n, 19, 20, 21, 22, 23, 24
	mov	x\n, #\n
	.endr
	.irp n, 8, 9, 10, 11, 12, 13, 14, 15
	mov	x12, #(0x800 + \n)
	fmov	d\n, x12
	.endr
	bl	ghash_user_blocks
	ldp	x0, x1, [sp]
	ldp	x2, x3, [sp, #16]
	ldr	x4, [sp, #32]
	mov	x5, #2
	bl	ghash_user_repeat
	mov	w0, #0
	.irp n, 19, 20, 21, 22, 23, 24
	cmp	x\n, #\n
	cset	w9, ne
	orr	w0, w0, w9
	.endr
	.irp n, 8, 9, 10, 11, 12, 13, 14, 15
	fmov	x9, d\n
	cmp	x9, #(0x800 + \n)
	cset	w9, ne
	orr	w0, w0, w9
	.endr
	add	sp, sp, #48
	restore_all
	ret
SYM_FUNC_END(ghash_user_abi_check)

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GHASH_USER_LINKAGE_H
#define GHASH_USER_LINKAGE_H
#define SYM_FUNC_START(name) \
	.text; .p2align 2; .global name; .hidden name; .type name, %function; name:
#define SYM_FUNC_END(name) .size name, . - name
	.section .note.GNU-stack, "", %progbits
#endif

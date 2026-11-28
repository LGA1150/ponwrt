# SPDX-License-Identifier: GPL-2.0-only
obj-m += ghash_neon_shuffle.o
ghash_neon_shuffle-y := ghash-neon-glue.o ghash-neon-core.o ghash-neon-mul.o \
			ghash-neon-intrinsics.o ghash-neon-schoolbook.o \
			ghash-neon-k32.o ghash-neon-k16.o ghash-neon-single-k32.o

CFLAGS_ghash-neon-intrinsics.o += $(CC_FLAGS_FPU)
CFLAGS_REMOVE_ghash-neon-intrinsics.o += $(CC_FLAGS_NO_FPU)

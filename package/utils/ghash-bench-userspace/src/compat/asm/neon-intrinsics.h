/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GHASH_USER_INTRINSICS_H
#define GHASH_USER_INTRINSICS_H
#include <arm_neon.h>
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#ifdef __AARCH64EB__
#define CONFIG_CPU_BIG_ENDIAN 1
#endif
#endif

# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=crypto-ghash-neon-shuffle
PKG_RELEASE:=36
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(wildcard $(CURDIR)/src/*)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/crypto-ghash-neon-shuffle
  SUBMENU:=Cryptographic API modules
  TITLE:=Standalone ARM64 NEON GHASH comparison drivers
  DEPENDS:=@aarch64 +kmod-crypto-hash +kmod-crypto-gf128
  FILES:=$(PKG_BUILD_DIR)/ghash_neon_shuffle.ko
endef

define KernelPackage/crypto-ghash-neon-shuffle/description
  Experimental GHASH implementation using ARM64 NEON byte polynomial
  multiplication, combined shuffles, scheduling, GPR input loads and
  two-block aggregation with shift or byte-PMULL reduction. Registers
  ghash-neon-shuffle, ghash-neon-sched, ghash-neon-gpr, ghash-neon-2x and
  ghash-neon-2x-pmull below the stock drivers' priority. ghash-neon-pmull
  tests the same byte-PMULL reducer with one block per iteration.
  ghash-neon-orig-pmull changes only the stock loop's reduction.
  ghash-neon-orig-pmull13 tests a 13-instruction version of that reducer.
  ghash-neon-orig-direct reduces raw Karatsuba products in 15 instructions.
  ghash-neon-schoolbook tests direct 128-bit products without Karatsuba.
  ghash-neon-4x-schoolbook shares their diagonal shuffles across four blocks.
  ghash-neon-4x-schoolbook-reuse alternates its rotated-key table traversal.
  ghash-neon-4x-schoolbook-load also overlaps scalar key loads with arithmetic.
  Nine compact-key single-block K32 variants compare scheduling, unrolling,
  scalar preloads, direct reduction and packing with a 16-byte key context.
  ghash-neon-4x-k32-compact keeps raw H and four powers in 80 bytes,
  building its rotated operands in 432 bytes of temporary stack storage.
  Four smaller-stack 4x K32 controls regenerate rotated operands from
  288/144-byte tables or retain them in GPRs with 32/0 bytes of stack.
  All four keep the same 80-byte persistent key layout.
  Five additional 4x variants remove input shuffles, reuse Karatsuba cross
  operands and test earlier preparation, without changing the key context.
  Seven ghash-neon-4x-k32 variants add a 32-bit Karatsuba split and compare
  scalar/SIMD key loads, reconstruction scheduling and fixed table shuffles.
  ghash-neon-8x-k32-tbl combines two K32 groups before reduction.
  ghash-neon-16x-k32 shares reduction across sixteen blocks; its -merge
  variant also shares reconstruction using 128 bytes of stack scratch.
  Five ghash-neon-8x-k32-no* variants replace the 128-bit or 64-bit
  Karatsuba split, or both, with direct products and compare shuffle timing.
  Five ghash-neon-8x-k32-merge variants also share inner reconstruction
  across eight blocks, comparing scalar/SIMD key loads and lane scheduling.
  ghash-neon-8x-k32-early and -lanes isolate scheduling on the previous loop.
  Four ghash-neon-{4x,8x}-k32-stp variants test scalar stores to stack
  scratch space followed by grouped or scheduled SIMD key reloads.
  ghash-neon-4x-gpr-reduce moves reduction shifts and XORs into scalar registers.
  ghash-neon-4x-stage overlaps scalar input preparation and stores with SIMD.
  ghash-neon-k32 and ghash-neon-k32-packed test single-block 32-bit splits.
  ghash-neon-4x-k16 and ghash-neon-4x-k8 test deeper Karatsuba splits,
  with separate -load variants overlapping scalar key loads with arithmetic.
  ghash-neon-intrinsics translates that single-block loop into C NEON intrinsics.
  ghash-neon-orig-pmull12 combines its byte extraction and rotation with TBL.
  ghash-neon-4x-pmull12 tests that reducer with four-block aggregation.
  ghash-neon-4x-pmull13 tests its UZP/EXT version with four-block aggregation.
  ghash-neon-fused folds part of reduction into a precomputed hash key.
  ghash-neon-2x-reuse retains the last key table between PMULL block pairs.
  ghash-neon-2x-load overlaps the remaining key-table loads with multiplication.
  ghash-neon-4x accumulates four blocks before reconstruction and reduction.
  ghash-neon-8x accumulates eight blocks, rereading input instead of spilling.
  ghash-neon-pmul tests truncated byte polynomial products with bit reversal.
  ghash-neon-umull tests the generic holes method with integer widening products.
  Coexists with built-in GHASH and needs no kernel or ghash-ce replacement.
  Load explicitly and select the driver by name for measurements.
endef

define Build/Prepare
	[ "$(KERNEL_PATCHVER)" = "6.18" ] || { \
		echo "crypto-ghash-neon-shuffle: this package requires Linux 6.18" >&2; exit 1; \
	}
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/*.c ./src/*.h ./src/*.S $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,crypto-ghash-neon-shuffle))

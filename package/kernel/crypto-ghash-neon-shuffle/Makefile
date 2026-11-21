# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=crypto-ghash-neon-shuffle
PKG_RELEASE:=16
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
  ghash-neon-orig-pmull12 combines its byte extraction and rotation with TBL.
  ghash-neon-4x-pmull12 tests that reducer with four-block aggregation.
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
	$(CP) ./src/Makefile ./src/*.c ./src/*.S $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,crypto-ghash-neon-shuffle))

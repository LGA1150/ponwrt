# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ghash-bench
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src/Makefile $(CURDIR)/src/ghash_bench.c

include $(INCLUDE_DIR)/package.mk

define KernelPackage/ghash-bench
  SUBMENU:=Cryptographic API modules
  TITLE:=GHASH Crypto API throughput benchmark
  DEPENDS:=+kmod-crypto-ghash +aarch64:kmod-crypto-ghash-neon-shuffle
  KCONFIG:=CONFIG_CRYPTO_LIB_AES=y
  FILES:=$(PKG_BUILD_DIR)/ghash_bench.ko
endef

define KernelPackage/ghash-bench/description
  Explicitly invoked GHASH throughput sweeps through the ahash API used
  by generic GCM. Compare selected implementations, including ARM64 NEON,
  with configurable lengths, queue depths and scatterlist chunk sizes.
  Verify digests against an independent software reference outside timing.
  Does not require kTLS or an EIP93 device. No automatic loading.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/ghash_bench.c $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,ghash-bench))

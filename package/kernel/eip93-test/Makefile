# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=eip93-test
PKG_RELEASE:=12
PKG_LICENSE:=GPL-2.0-or-later
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src/eip93_test.c $(CURDIR)/src/eip93_ctr_bench.c \
	$(CURDIR)/src/eip93_gcm_sg_test.c $(CURDIR)/src/algorithms.h \
	$(CURDIR)/src/vectors.h $(CURDIR)/src/Makefile

include $(INCLUDE_DIR)/package.mk

define KernelPackage/eip93-test
  SUBMENU:=Cryptographic API modules
  TITLE:=EIP93 Crypto API correctness and CTR throughput tests
  DEPENDS:=+kmod-crypto-authenc +kmod-crypto-des \
	+kmod-crypto-md5 +kmod-crypto-sha1 +kmod-crypto-sha256 \
	+kmod-crypto-cbc +kmod-crypto-ecb +kmod-crypto-ctr +kmod-crypto-gcm
  FILES:=$(PKG_BUILD_DIR)/eip93_test.ko $(PKG_BUILD_DIR)/eip93_ctr_bench.ko \
	$(PKG_BUILD_DIR)/eip93_gcm_sg_test.ko
endef

define KernelPackage/eip93-test/description
  Explicitly invoked Crypto API tests for EIP93 skciphers, hashes, HMACs,
  authenc and GCM. Compares hardware driver implementations with explicitly
  selected software references across lengths, keys, IVs and SG layouts.
  Includes a separate aligned CTR length and queue-depth throughput sweep,
  and a fragmented-input, aligned-output GCM correctness matrix.
  Load the EIP93 driver before running this module. No automatic loading.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/eip93_test.c ./src/eip93_ctr_bench.c \
		./src/eip93_gcm_sg_test.c ./src/algorithms.h ./src/vectors.h \
		./src/Makefile $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,eip93-test))

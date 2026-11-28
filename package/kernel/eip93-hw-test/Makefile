# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=eip93-hw-test
PKG_RELEASE:=11
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src/Makefile $(CURDIR)/src/eip93_hw_test.c \
	$(CURDIR)/src/eip93_hw.h

include $(INCLUDE_DIR)/package.mk

define KernelPackage/eip93-hw-test
  SUBMENU:=Cryptographic API modules
  TITLE:=Standalone EIP93 NULL-copy and AES-CTR hardware tests
  DEPENDS:=@USES_DEVICETREE @!BIG_ENDIAN
  KCONFIG:=CONFIG_CRYPTO_LIB_AES=y
  FILES:=$(PKG_BUILD_DIR)/eip93_hw_test.ko
endef

define KernelPackage/eip93-hw-test/description
  Explicitly invoked NULL-copy and AES-CTR tests and throughput
  sweeps using EIP93 registers and descriptor rings directly. Uses the
  software AES library for verification, without the EIP93 crypto driver.
  Supports shifting source and destination DMA addresses independently.
  The device must be unbound from its normal driver before loading.
  No automatic loading.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/eip93_hw_test.c ./src/eip93_hw.h $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,eip93-hw-test))

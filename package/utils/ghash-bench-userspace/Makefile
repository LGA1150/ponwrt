# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=ghash-bench-userspace
PKG_VERSION:=1.0
PKG_RELEASE:=16
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
GHASH_SOURCE_DIR:=$(TOPDIR)/package/kernel/crypto-ghash-neon-shuffle/src
PKG_FILE_DEPENDS:=$(wildcard $(CURDIR)/src/* $(CURDIR)/src/compat/*/*.h) \
	$(wildcard $(GHASH_SOURCE_DIR)/*.S $(GHASH_SOURCE_DIR)/ghash-neon-intrinsics.*)

include $(INCLUDE_DIR)/package.mk

define Package/ghash-bench-userspace
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Userspace ARM64 GHASH correctness and throughput tests
  DEPENDS:=@aarch64
endef

define Package/ghash-bench-userspace/description
  Compare the existing NEON GHASH experiments directly in userspace,
  with a portable reference, known answers, guarded boundary tests,
  CPU affinity and CSV measurements. No Crypto API or module required.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/common
	$(CP) ./src/. $(PKG_BUILD_DIR)/
	$(CP) $(GHASH_SOURCE_DIR)/*.S \
		$(GHASH_SOURCE_DIR)/ghash-neon-intrinsics.c \
		$(GHASH_SOURCE_DIR)/ghash-neon-intrinsics.h $(PKG_BUILD_DIR)/common/
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) GHASH_SRC=common \
		CC="$(TARGET_CC)" CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/ghash-bench-userspace/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ghash-bench $(1)/usr/bin/
endef

$(eval $(call BuildPackage,ghash-bench-userspace))

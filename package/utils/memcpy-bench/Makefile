# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=memcpy-bench
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src/Makefile $(CURDIR)/src/memcpy_bench.c

include $(INCLUDE_DIR)/package.mk

define Package/memcpy-bench
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Streaming userspace memcpy throughput benchmark
endef

define Package/memcpy-bench/description
  Standalone libc memcpy measurements with blocks up to 128 MiB,
  configurable working set and CPU affinity, CSV output and untimed
  data verification. No dependency on the EIP93 or kTLS modules.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/memcpy_bench.c $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/memcpy-bench/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/memcpy-bench $(1)/usr/bin/
endef

$(eval $(call BuildPackage,memcpy-bench))

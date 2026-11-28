# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=ktls-bench
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src/Makefile $(CURDIR)/src/tls_rx_bench.c

include $(INCLUDE_DIR)/package.mk

define Package/ktls-bench
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Standalone kTLS RX/TX throughput benchmark
  DEPENDS:=+libpthread +kmod-crypto-gcm +kmod-crypto-ccm
endef

define Package/ktls-bench/description
  Local loopback kTLS benchmark for TLS 1.2 and 1.3, AES-GCM-128/256
  and AES-CCM-128. Measures recv, send and splice with multiple worker
  threads, payload verification, CPU accounting and CSV output.
  Requires a kernel with TLS support or either kmod-tls or kmod-ktls.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/tls_rx_bench.c $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/ktls-bench/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ktls-bench $(1)/usr/bin/
endef

$(eval $(call BuildPackage,ktls-bench))

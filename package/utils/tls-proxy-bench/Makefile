# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=tls-proxy-bench
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_FILE_DEPENDS:=$(CURDIR)/src

include $(INCLUDE_DIR)/package.mk

define Package/tls-proxy-bench
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Verified TLS-to-TCP proxy throughput benchmark
  DEPENDS:=+libopenssl +libpthread
endef

define Package/tls-proxy-bench/description
  A standalone OpenSSL client and plaintext TCP backend in one process.
  Tests TLS proxy RX, TX and simultaneous traffic with multiple connections
  and byte-for-byte payload verification. Can also be built directly on a PC.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/tls_proxy_bench.c $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/tls-proxy-bench/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tls-proxy-bench $(1)/usr/bin/
endef

$(eval $(call BuildPackage,tls-proxy-bench))

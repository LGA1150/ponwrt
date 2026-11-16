# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ktls
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1
PKG_EXTMOD_SUBDIRS:=net/tls

# Follow the target's stable kernel and distribution patches. Only source
# files are copied, so no in-tree object files enter the external build.
PKG_FILE_DEPENDS:=$(wildcard $(LINUX_DIR)/net/tls/*.[ch]) \
	$(LINUX_DIR)/net/tls/Makefile $(LINUX_DIR)/include/net/tls.h

include $(INCLUDE_DIR)/package.mk

define KernelPackage/ktls
  SUBMENU:=Network Support
  TITLE:=Out-of-tree kTLS with asynchronous record pipelining
  DEPENDS:=+kmod-crypto-gcm +kmod-crypto-ccm
  CONFLICTS:=kmod-tls
  KCONFIG:= \
	CONFIG_TLS=m \
	CONFIG_TLS_DEVICE=n \
	CONFIG_TLS_TOE=n
  FILES:=$(PKG_BUILD_DIR)/net/tls/tls.ko
endef

define KernelPackage/ktls/description
  Software kTLS for TLS 1.2 and TLS 1.3, including asynchronous receive
  pipelining and transmit fixes. Crypto API accelerators are supported.
  Replaces kmod-tls; NIC TLS offload and TCP stack bypass are disabled.
  The package is based on the prepared Linux 6.18 kernel sources.
endef

define Build/Prepare
	[ "$(KERNEL_PATCHVER)" = "6.18" ] || { \
		echo "ktls: this patch series requires Linux 6.18" >&2; exit 1; \
	}
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/net/tls $(PKG_BUILD_DIR)/include/net
	$(CP) $(LINUX_DIR)/net/tls/*.[ch] $(LINUX_DIR)/net/tls/Makefile \
		$(PKG_BUILD_DIR)/net/tls/
	$(CP) $(LINUX_DIR)/include/net/tls.h $(PKG_BUILD_DIR)/include/net/
	$(Build/Patch)
endef

define Build/Configure
endef

# Put the package's patched net/tls.h before the kernel include directories.
# Its receive context is private to software kTLS; do not use it with NIC
# offload drivers compiled against the base kernel's different RX layout.
define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/net/tls" \
		NOSTDINC_FLAGS="$(KERNEL_NOSTDINC_FLAGS) -I$(PKG_BUILD_DIR)/include" \
		modules
endef

$(eval $(call KernelPackage,ktls))

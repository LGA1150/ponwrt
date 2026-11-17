# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=eip93
PKG_RELEASE:=7
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_PARALLEL:=1

EIP93_SOURCE_DIR:=$(LINUX_DIR)/drivers/crypto/inside-secure/eip93
PKG_FILE_DEPENDS:=$(wildcard $(EIP93_SOURCE_DIR)/*.[ch]) $(EIP93_SOURCE_DIR)/Makefile

include $(INCLUDE_DIR)/package.mk

define KernelPackage/eip93
  SUBMENU:=Cryptographic API modules
  TITLE:=Out-of-tree EIP93 crypto accelerator
  DEPENDS:=@(TARGET_airoha||TARGET_ramips_mt7621) \
	+kmod-crypto-authenc +kmod-crypto-des +kmod-crypto-md5 \
	+kmod-crypto-sha1 +kmod-crypto-sha256 \
	+kmod-crypto-cbc +kmod-crypto-ecb +kmod-crypto-ctr
  CONFLICTS:=kmod-crypto-hw-eip93
  KCONFIG:= \
	CONFIG_CRYPTO_HW=y \
	CONFIG_CRYPTO_DEV_EIP93=m \
	CONFIG_CRYPTO_DEV_EIP93_GENERIC_SW_MAX_LEN=256 \
	CONFIG_CRYPTO_DEV_EIP93_AES_128_SW_MAX_LEN=512
  FILES:=$(PKG_BUILD_DIR)/crypto-hw-eip93.ko
endef

define KernelPackage/eip93/description
  EIP93 Crypto API driver for Airoha and MediaTek MT7621, built outside
  the kernel source tree. Retains the target kernel's stable fixes and
  PonWrt patches, including software fallback for small AES requests.
  Queues async requests and waits for hardware ring and request-ID credits.
  Replaces kmod-crypto-hw-eip93 and can be patched and rebuilt separately.
endef

define Build/Prepare
	[ "$(KERNEL_PATCHVER)" = "6.18" ] || { \
		echo "eip93: this package requires the Linux 6.18 driver" >&2; exit 1; \
	}
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(EIP93_SOURCE_DIR)/*.[ch] $(EIP93_SOURCE_DIR)/Makefile $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Configure
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,eip93))

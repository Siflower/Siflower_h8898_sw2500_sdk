# Copyright (C) 2020 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ctc21101
PKG_VERSION:=261014
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/ctc21101
   SUBMENU:=Network Devices
   TITLE:=CTC21101 PHY driver
   FILES:= \
        $(PKG_BUILD_DIR)/ctc21101.ko
   AUTOLOAD:=$(call AutoLoad,20,ctc21101)
endef

define KernelPackage/ctc21101/description
   Supports the CTC21101 PHY.
endef

define Build/Compile
        $(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
endef

$(eval $(call KernelPackage,ctc21101))

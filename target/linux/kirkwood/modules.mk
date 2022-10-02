define KernelPackage/ata-marvell-sata
  TITLE:=Marvell Serial ATA support
  DEPENDS:=@TARGET_kirkwood
  KCONFIG:=CONFIG_SATA_MV CONFIG_SATA_PMP=y
  FILES:=$(LINUX_DIR)/drivers/ata/sata_mv.ko
  AUTOLOAD:=$(call AutoLoad,41,sata_mv,1)
  $(call AddDepends/ata)
endef

define KernelPackage/ata-marvell-sata/description
 SATA support for marvell chipsets
endef

$(eval $(call KernelPackage,ata-marvell-sata))

define KernelPackage/mfd-landisk-r8c
  SUBMENU:=$(OTHER_MENU)
  TITLE:=R8C support on I-O DATA LAN DISK series NAS
  DEPENDS:= @TARGET_kirkwood
  KCONFIG:= \
	CONFIG_SERIAL_DEV_BUS=y \
	CONFIG_SERIAL_DEV_CTRL_TTYPORT=y \
	CONFIG_MFD_LANDISK_R8C
  FILES:= \
	$(LINUX_DIR)/drivers/mfd/landisk-r8c.ko
  AUTOLOAD:=$(call AutoProbe,landisk-r8c,1)
endef

define KernelPackage/mfd-landisk-r8c/description
 support for R8C MCU on I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,mfd-landisk-r8c))

define KernelPackage/hwmon-landisk-r8c
  TITLE:=R8C hwmon support on I-O DATA LAN DISK series NAS
  KCONFIG:= CONFIG_SENSORS_LANDISK_R8C_HWMON
  FILES:= \
	$(LINUX_DIR)/drivers/hwmon/landisk-r8c-hwmon.ko
  AUTOLOAD:=$(call AutoProbe,landisk-r8c-hwmon)
  $(call AddDepends/hwmon,+kmod-mfd-landisk-r8c @TARGET_kirkwood)
endef

define KernelPackage/hwmon-landisk-r8c/description
 Hardware monitoring support for R8C MCU on I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,hwmon-landisk-r8c))

define KernelPackage/input-landisk-r8c-keys
  SUBMENU:=$(INPUT_MODULES_MENU)
  TITLE:=R8C key support on I-O DATA LAN DISK series NAS
  DEPENDS:= +kmod-mfd-landisk-r8c +kmod-input-core @TARGET_kirkwood
  KCONFIG:= CONFIG_INPUT_LANDISK_R8C_KEYS
  FILES:= \
	$(LINUX_DIR)/drivers/input/misc/landisk-r8c-keys.ko
  AUTOLOAD:=$(call AutoProbe,landisk-r8c-keys,1)
endef

define KernelPackage/input-landisk-r8c-keys/description
 key support for R8C MCU on I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,input-landisk-r8c-keys))

define KernelPackage/mvsdio
  SUBMENU:=$(OTHER_MENU)
  TITLE:=Marvell MMC/SD/SDIO host driver
  DEPENDS:=+kmod-mmc @TARGET_kirkwood
  KCONFIG:= CONFIG_MMC_MVSDIO
  FILES:= \
	$(LINUX_DIR)/drivers/mmc/host/mvsdio.ko
  AUTOLOAD:=$(call AutoProbe,mvsdio,1)
endef

define KernelPackage/mvsdio/description
 Kernel support for the Marvell SDIO host driver.
endef

$(eval $(call KernelPackage,mvsdio))

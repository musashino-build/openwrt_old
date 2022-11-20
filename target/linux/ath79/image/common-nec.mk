DEVICE_VARS += NEC_BIN_NAME

define Build/nec-data-header
  $(eval hdrflags=$(word 1,$(1)))
  $(eval hdrldaddr=$(if $(word 2,$(1)),$(word 2,$(1)),$(KERNEL_LOADADDR)))
  # padding image
  dd if=$@ of=$@.pad bs=4 conv=sync 2>/dev/null
  # add nec header
  ( \
    nec_fw_size="$$(($$(stat -c%s $@.pad) + 0x18))"; \
    printf $$(printf "%08x%08x0000001800000000%08x%08x" \
              $(hdrflags) $${nec_fw_size} $(hdrldaddr) $(hdrldaddr) | \
              sed 's/../\\x&/g'); \
    dd if=$@.pad; \
  ) > $@
  # calcurate and add checksum
  ( \
    cksum=$$( \
      dd if=$@ ibs=4 skip=1 | od -A n -t u2 --endian=little | tr -s ' ' '\n' | \
        awk '{s+=$$0}END{printf "%04x", 0xffff-(s%0x100000000)%0xffff}'); \
    printf "\x$${cksum:2:2}\x$${cksum:0:2}" | \
      dd of=$@ conv=notrunc bs=2 seek=6 count=1; \
  )
  rm -f $@.pad
endef

define Build/append-ff
  $(eval blksize=$(word 1,$(1)))
  $(eval image=$(if $(word 2,$(1)),$(word 2,$(1)),$@))
  dd if=/dev/zero bs=$(blksize) count=1 | tr "\0" "\377" >> $(image)
endef

define Build/nec-fs-header
  $(eval imgname=$(word 1,$(1)))
  $(eval imglen=$(word 2,$(1)))
  printf "$(imgname)" | dd bs=16 count=1 conv=sync >> $@
  printf $$(printf "00000000%08x30534654ffffffff" $(imglen) | \
    sed 's/../\\x&/g') >> $@
  $(call Build/append-ff,32)
endef

define Build/nec-bootfs
  printf "USB ATERMWL3050\x00" > $@.cat
  $(call Build/append-ff,16 $@.cat)
  cat $(1) >> $@.cat
  ( \
    echo -e "Binary $(NEC_BIN_NAME) File END \r" > $@.binend; \
    pad_len=$$((0x20 - $$(stat -c%s $@.binend))); \
    dd if=/dev/zero bs=$$pad_len count=1 2>/dev/null | \
      tr "\0" "\377" >> $@.cat; \
    cat $@.binend >> $@.cat; \
    \
    bootdata_len=$$(stat -c%s $@.cat); \
    blks=$$((bootdata_len / 0xffc0)); \
    lastblk_len=$$((bootdata_len % 0xffc0)); \
    [ $$lastblk_len -gt 0 ] && blks=$$((blks + 1)); \
    for i in $$(seq 0 $$((blks - 1))); do \
      printf "Firmware\x00\xff\xff\xff\xff\xff\xff\xff" >> $@; \
      printf $$(printf "%08x%08x30534654ffffffff" $$i $$bootdata_len | \
                sed 's/../\\x&/g') >> $@; \
      dd if=/dev/zero bs=32 count=1 2>/dev/null | tr "\0" "\377" >> $@; \
      dd if=$@.cat bs=$$((0xffc0)) count=1 skip=$$i 2>/dev/null >> $@; \
    done; \
    dd if=/dev/zero bs=$$((0xffc0 - lastblk_len)) count=1 2>/dev/null | \
      tr "\0" "\377" >> $@; \
  )
  #rm $@.cat $@.binend
endef

define Build/append-string-esc
  echo -en $(1) >> $@
endef

define Build/nec-null-bin
  rm -f $@
  touch $@
endef

define Build/remove-uimage-header
  dd if=$@ of=$@.new iflag=skip_bytes skip=64 2>/dev/null
  mv $@.new $@
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
  LOADER_TYPE := bin
  LOADER_FLASH_OFFS := 0x40000
  COMPILE := infoblock-$(1).bin loader-$(1).bin endblock-$(1).bin
  COMPILE/infoblock-$(1).bin := nec-null-bin | \
	append-string-esc MIPS $(VERSION_DIST) Linux-$(LINUX_VERSION)\n | \
	append-string-esc $(REVISION)\n |\
	nec-data-header 0x0000ffff 0x0
  COMPILE/loader-$(1).bin := loader-okli-compile | nec-data-header 0x0002fffd
  COMPILE/endblock-$(1).bin := nec-null-bin | nec-data-header 0x0001fffe
  IMAGES += ldr-sysupgrade.bin loader.bin
  IMAGE/ldr-sysupgrade.bin := \
	nec-bootfs $(KDIR)/infoblock-$(1).bin bin/nec_aterm_dummy_tp.bin \
		$(KDIR)/loader-$(1).bin $(KDIR)/endblock-$(1).bin | \
	append-ff $$$$(BLOCKSIZE) | \
	append-kernel | pad-to $$$$(BLOCKSIZE) | append-rootfs | check-size | \
	append-metadata
  IMAGE/loader.bin := \
	nec-bootfs $(KDIR)/infoblock-$(1).bin bin/nec_aterm_dummy_tp.bin \
		$(KDIR)/loader-$(1).bin $(KDIR)/endblock-$(1).bin
ifneq ($(CONFIG_TARGET_ROOTFS_INITRAMFS),)
  ARTIFACTS := initramfs-necimg.bin
  ARTIFACT/initramfs-necimg.bin := append-image initramfs-kernel.bin | \
	remove-uimage-header | loader-kernel | nec-data-header 0x0002fffd
  DEVICE_PACKAGES := kmod-usb2
endif
endef

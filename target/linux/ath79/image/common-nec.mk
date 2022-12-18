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

define Build/nec-bsdfw
  printf "USB ATERMWL3050\x00" > $@.cat
  dd if=/dev/zero bs=16 count=1 | tr "\0" "\377" >> $@.cat
  cat $(1) >> $@.cat
  ( \
    echo -e "Binary $(NEC_BIN_NAME) File END \r" > $@.binend; \
    pad_len=$$((0x20 - $$(stat -c%s $@.binend))); \
    dd if=/dev/zero bs=$$pad_len count=1 2>/dev/null | \
      tr "\0" "\377"; \
    cat $@.binend; \
  ) >> $@.cat
  mv $@.cat $@
  rm $@.binend
endef

define Build/nec-bootfs
  ( \
    bootdata_len=$$(stat -c%s $@); \
    blks=$$((bootdata_len / $(BLOCKSIZE))); \
    lastblk_len=$$((bootdata_len % $(BLOCKSIZE))); \
    [ $$lastblk_len -gt 0 ] && blks=$$((blks + 1)); \
    for i in $$(seq 0 $$((blks - 1))); do \
      printf "Firmware\x00\xff\xff\xff\xff\xff\xff\xff" >> $@.new; \
      printf $$(printf "%08x%08x30534654ffffffff" $$i $$bootdata_len | \
                sed 's/../\\x&/g') >> $@.new; \
      dd if=/dev/zero bs=32 count=1 2>/dev/null | tr "\0" "\377" >> $@.new; \
      dd if=$@ bs=$$(($(BLOCKSIZE))) count=1 skip=$$i 2>/dev/null >> $@.new; \
    done; \
    if [ $$lastblk_len -gt 0 ]; then \
      dd if=/dev/zero bs=$$(($(BLOCKSIZE) - lastblk_len)) count=1 2>/dev/null | \
        tr "\0" "\377" >> $@.new; \
    fi; \
  )
  mv $@.new $@
endef

define Build/append-string-esc
  echo -e $(1) >> $@
endef

define Build/nec-null-bin
  rm -f $@
  touch $@
endef

# pad-rootfs requires the blocksize in KiB, but Aterm's blocksize
# cannot be divided by 1024
define Build/nec-pad-rootfs
  ( \
    fw_len=$$(stat -c%s $@); \
    pad_len=$$(($(BLOCKSIZE) - fw_len % $(BLOCKSIZE))); \
    [ $$pad_len -gt 0 ] && \
      dd if=/dev/zero bs=$$pad_len count=1 2>/dev/null | tr "\0" "\377"; \
  ) >> $@
  printf "\xde\xad\xc0\xde" >> $@
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
  BLOCKSIZE := 65472
  LOADER_TYPE := bin
  KERNEL := kernel-bin | append-dtb | lzma | loader-kernel | \
	nec-data-header 0x0002fffd
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | loader-kernel | \
	nec-data-header 0x0002fffd
  COMPILE := infoblock-$(1).bin loader-$(1).bin endblock-$(1).bin
  COMPILE/infoblock-$(1).bin := nec-null-bin | \
	append-string-esc MIPS $(VERSION_DIST) Linux-$(LINUX_VERSION) | \
	append-string-esc $(REVISION) |\
	nec-data-header 0x0000ffff 0x0
  COMPILE/loader-$(1).bin := loader-okli-compile | nec-data-header 0x0002fffd
  COMPILE/endblock-$(1).bin := nec-null-bin | nec-data-header 0x0001fffe
  IMAGES += full.bin loader.bin
  IMAGE/full.bin := append-kernel | \
	nec-bsdfw bin/wr8750n_1_0_9_tp.bin $$$$@ $(KDIR)/endblock-$(1).bin | \
	pad-to $$$$(BLOCKSIZE) | \
	append-rootfs | nec-pad-rootfs | check-size | pad-to $$$$(IMAGE_SIZE) | \
	nec-bootfs
  IMAGE/sysupgrade.bin := append-kernel | \
	nec-bsdfw bin/wr8750n_1_0_9_tp.bin $$$$@ $(KDIR)/endblock-$(1).bin | \
	pad-to $$$$(BLOCKSIZE) | \
	append-rootfs | nec-pad-rootfs | check-size | append-metadata
  IMAGE/loader.bin := \
	nec-bsdfw $(KDIR)/infoblock-$(1).bin bin/nec_aterm_dummy_tp.bin \
		$(KDIR)/loader-$(1).bin $(KDIR)/endblock-$(1).bin | nec-bootfs
  DEVICE_PACKAGES := kmod-usb2
endef

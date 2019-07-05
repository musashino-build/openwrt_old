define Build/nec-boot-header
  # padding image
  dd if=$@ of=$@.pad bs=4 conv=sync 2>/dev/null
  # add nec header
  ( \
    nec_fw_size=$$(printf '%08x' "$$(($$(stat -c%s $@.pad) + 0x18))"); \
    echo -ne $$(echo "0002FFFD$${nec_fw_size}00000018000000008006000080060000" | \
      sed 's/../\\x&/g'); \
    dd if=$@.pad; \
  ) > $@
  # calcurate and add checksum
  ( \
    cksum=$$( \
      dd if=$@ ibs=4 skip=1 | od -A n -t u2 --endian=little | tr -s ' ' '\n' | \
        awk '{s+=$$0}END{printf "%04x", 0xffff-(s%0x100000000)%0xffff}'); \
    echo -ne "\x$${cksum:2:2}\x$${cksum:0:2}" | \
      dd of=$@ conv=notrunc bs=2 seek=6 count=1; \
  )
  rm -f $@.pad
endef

define Build/remove-uimage-header
  dd if=$@ of=$@.new iflag=skip_bytes skip=64 2>/dev/null
  mv $@.new $@
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
ifneq ($(CONFIG_TARGET_ROOTFS_INITRAMFS),)
  LOADER_TYPE := bin
  ARTIFACTS := initramfs-necimg.bin
  ARTIFACT/initramfs-necimg.bin := append-image initramfs-kernel.bin | \
    remove-uimage-header | loader-kernel | nec-boot-header
  DEVICE_PACKAGES := kmod-usb2
endif
endef

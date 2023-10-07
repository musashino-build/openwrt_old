. /lib/functions.sh

fortinet_fwinfo_blocks() {
	local fwinfo_mtd="$1"
	local offset="$2"
	local val="$3"
	local blks

	blks=$((val / 0x200))
	[ $((val % 0x200)) -gt 0 ] && blks=$((blks + 1))
	blks=$(printf "%08x" $blks)
	printf "fwinfo: offset-> 0x%x, blocks-> 0x%s (val: 0x%08x)\n" \
		$offset $blks $val

	printf "\x${blks:6:2}\x${blks:4:2}\x${blks:2:2}\x${blks:0:2}" | \
		dd bs=4 count=1 seek=$((offset / 4)) conv=notrunc of=${fwinfo_mtd}
}

fortinet_do_upgrade() {
	local fwinfo_mtd="$(find_mtd_part firmware-info)"
	local board_dir="$(tar tf "$1" | grep -m 1 '^sysupgrade-.*/$')"
	local kern_len_ofs root_len_ofs
	local kern_len root_len
	local part_index

	board_dir="${board_dir%/}"

	if [ -z "$fwinfo_mtd" ]; then
		v "ERROR: MTD device \"firmware-info\" not found"
		return 1
	fi

	part_index=$(hexdump -n 1 -s $((0x170)) -e '"%d"' $fwinfo_mtd)
	case $part_index in
	0)
		kern_len_ofs=0x184
		root_len_ofs=0x18c
		PART_NAME="firmware"
		;;
	1)
		kern_len_ofs=0x194
		root_len_ofs=0x19c
		PART_NAME="firmware2"
		;;
	*)
		return 1
	esac

	tar xOf "$1" "$board_dir/kernel" > /tmp/fw_kernel.bin 2>/dev/null
	tar xOf "$1" "$board_dir/root" > /tmp/fw_root.bin 2>/dev/null
	# pad kernel
	dd if=/tmp/fw_kernel.bin of=/tmp/fw_kernel.pad \
		bs=$((0x10000)) conv=sync 2>/dev/null
	# concat kernel and rootfs
	cat /tmp/fw_kernel.pad /tmp/fw_root.bin > /tmp/fw.bin

	kern_len=$(wc -c /tmp/fw_kernel.pad | cut -d' ' -f1)
	root_len=$(wc -c /tmp/fw_root.bin | cut -d' ' -f1)

	if [ -z "$kern_len" ] || [ -z "$root_len" ]; then
		v "ERROR: failed to get length of new kernel or rootfs"
		return 1
	fi

	fortinet_fwinfo_blocks "$fwinfo_mtd" "$kern_len_ofs" "$kern_len" || return 1
	fortinet_fwinfo_blocks "$fwinfo_mtd" "$root_len_ofs" "$root_len" || return 1

	default_do_upgrade "/tmp/fw.bin"
}

fortinet_set_active_part() {
	local fwinfo_mtd="$(find_mtd_part firmware-info)"
	local part_name="$1"
	local index
	local imgname_seek

	if [ -z "$fwinfo_mtd" ]; then
		echo "ERROR: MTD device \"firmware-info\" not found"
		return 1
	fi

	case "$part_name" in
	firmware)
		index=0
		imgname_seek=1
		;;
	firmware2)
		index=1
		imgname_seek=3
		;;
	*)
		echo "invalid partition name specified"
		return 1
		;;
	esac

	echo -n "\x$index" | \
		dd of=$fwinfo_mtd bs=1 seek=$((0x170)) count=1 conv=notrunc \
			2>/dev/null
}

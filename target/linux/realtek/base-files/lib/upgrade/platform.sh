PART_NAME=firmware
REQUIRE_IMAGE_METADATA=1

RAMFS_COPY_BIN='fw_printenv fw_setenv fw_printsys'
RAMFS_COPY_DATA='/etc/fw_env.config /etc/fw_sys.config /var/lock/fw_printenv.lock'

# The TP-Link bootloader gets its flash layout from "bootargs".
# Use this to our advantage, and:
#  1. Disable the second rootfs (usrimg2)
#  2. Extend the first rootfs (usrimg1) to include more space
#  3. Increase the baudrate to the expected 115200
tplink_sg2xxx_fix_mtdparts() {
	local args
	args="bootargs mtdparts=spi_flash:896K(boot),128K(env),6144K(sys)"
	args="$args,20480K(usrimg1@main),0K(usrimg2)"
	args="$args,4096K(usrappfs),1024K(para)\n"
	args="$args baudrate 115200"

	echo -e "$args" | fw_setenv --script -
}

# Get the current active firmware partition by image index
# got from system partition (u-boot-env2).
#  $1: key name
#  $2: index values of 1st and 2nd
#      (format: <1st>:<2nd>, ex.: 1:2)
#  $3: firmware partitions of 1st and 2nd
#      (format: <1st>:<2nd>, ex.: firmware:firmware2, default: firmware:firmware2)
sys_get_image_part() {
	local key=$1
	local val=$2 _val
	local part=${3:-firmware:firmware2}

	_val=$(fw_printsys -n "$key")
	[ -z "$_val" ] &&
		echo "failed to get value of $key" 1>&2 && return 1

	case "$_val" in
	"${val/:*/}") echo "${part/:*/}" && return ;;
	"${val/*:/}") echo "${part/*:/}" && return ;;
	esac

	echo "invalid index number! $_val" 1>&2
	return 1
}

platform_check_image() {
	return 0
}

platform_do_upgrade() {
	local board=$(board_name)

	case "$board" in
	tplink,sg2008p-v1|\
	tplink,sg2210p-v3)
		tplink_sg2xxx_fix_mtdparts
		default_do_upgrade "$1"
		;;
	apresia,aplgs120gtss)
		PART_NAME=$(sys_get_image_part Image_Id 1:2) || exit 1
		default_do_upgrade "$1"
		;;
	*)
		default_do_upgrade "$1"
		;;
	esac
}

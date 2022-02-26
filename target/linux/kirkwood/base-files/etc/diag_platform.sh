. /lib/functions.sh

set_led_state_platform() {
	local target

	case "$(board_name)" in
	iodata,hdl2-a)
		target="/dev/ttyS1"
		[ ! -w "$target" ] && return

		case "$1" in
		failsafe)
			echo ":sts err" > "$target"
			;;
		preinit_regular)
			# setting "on" required before setting other state
			# on the first change
			echo ":sts on" > "$target"
			echo ":sts notify" > "$target"
			;;
		upgrade)
			echo ":sts notify" > "$target"
			;;
		done)
			echo ":sts on" > "$target"
			;;
		esac
		;;
	esac
}

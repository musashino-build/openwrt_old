. /lib/functions.sh

set_led_state_platform() {
	local target

	case "$(board_name)" in
	iodata,hdl-a|\
	iodata,hdl2-a)
		target="/sys/bus/serial/devices/serial0-0/f1012100.serial:mcu:leds/status_mode"
		[ ! -w "$target" ] && return

		case "$1" in
		failsafe)
			echo "err" > "$target"
			;;
		preinit_regular)
			# setting "on" required before setting other state
			# on the first change
			echo "on" > "$target"
			echo "notify" > "$target"
			;;
		upgrade)
			echo "notify" > "$target"
			;;
		done)
			echo "on" > "$target"
			;;
		esac
		;;
	esac
}

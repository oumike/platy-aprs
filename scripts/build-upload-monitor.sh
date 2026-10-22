#!/bin/bash
# Build, upload and monitor a firmware target.
#
#   scripts/build-upload-monitor.sh                  device menu, then build + upload + monitor
#   scripts/build-upload-monitor.sh --wio-tracker-l2 [--port /dev/cu.usbmodem101]
#   scripts/build-upload-monitor.sh -B               build every target, print a pass/fail summary
#
# Options:
#   -9, --m9               Elecrow ThinkNode M9
#   -w, --wio-tracker-l2   Seeed Wio Tracker L2
#   -t, --tdeck-plus       LilyGo T-Deck Plus
#   -m, --mesh-deck        Attaky Mesh Deck (left halted after upload: power-cycle it)
#   -3, --crowpanel-35     Elecrow CrowPanel Advance 3.5
#   -l, --tlora-pager      LilyGo T-LoRa Pager
#   -p, --port PORT        upload/monitor port (default: PlatformIO auto-detect)
#   -e, --erase            erase flash before uploading (wipes config; M9: one esptool session)
#   -c, --fullclean        full clean before building
#   -d, --debug            debug build (-DPLATY_SERIAL_KEYS: raw key codes from the serial console)
#   -B, --just-build       build only; with no device flag, sweep every target
#   -n, --no-monitor       upload without opening the monitor
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ENV_NAME=""
PORT=""
ERASE=false
FULLCLEAN=false
DEBUG=false
JUST_BUILD=false
MONITOR=true

# Every firmware [env:NAME] in platformio.ini (host-side 'native' excluded), so
# a new target is covered by --just-build without touching this script.
all_envs() { sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini | grep -v '^native$'; }

env_label() {
	case "$1" in
		m9)             echo "Elecrow ThinkNode M9" ;;
		wio-tracker-l2) echo "Seeed Wio Tracker L2" ;;
		tdeck-plus)     echo "LilyGo T-Deck Plus" ;;
		mesh-deck)      echo "Attaky Mesh Deck" ;;
		crowpanel-35)   echo "Elecrow CrowPanel Advance 3.5" ;;
		tlora-pager)    echo "LilyGo T-LoRa Pager" ;;
		*)              echo "$1" ;;
	esac
}

format_duration() {
	local t="$1"
	if (( t >= 3600 )); then printf '%dh %02dm %02ds' $((t / 3600)) $((t % 3600 / 60)) $((t % 60))
	else printf '%dm %02ds' $((t / 60)) $((t % 60)); fi
}

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-9|--m9)             ENV_NAME="m9" ;;
		-w|--wio-tracker-l2) ENV_NAME="wio-tracker-l2" ;;
		-t|--tdeck-plus)     ENV_NAME="tdeck-plus" ;;
		-m|--mesh-deck)      ENV_NAME="mesh-deck" ;;
		-3|--crowpanel-35)   ENV_NAME="crowpanel-35" ;;
		-l|--tlora-pager)    ENV_NAME="tlora-pager" ;;
		-p|--port)           PORT="${2:?--port needs a value}"; shift ;;
		-e|--erase)          ERASE=true ;;
		-c|--fullclean)      FULLCLEAN=true ;;
		-d|--debug)          DEBUG=true ;;
		-B|--just-build)     JUST_BUILD=true ;;
		-n|--no-monitor)     MONITOR=false ;;
		-h|--help)           usage 0 ;;
		*) echo "Unknown option: $1" >&2; usage 1 ;;
	esac
	shift
done

if $DEBUG; then
	export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS:-} -DPLATY_SERIAL_KEYS"
fi

# Build sweep: every target, keep going on failure, summarise at the end.
if $JUST_BUILD && [[ -z "$ENV_NAME" ]]; then
	declare -a results=()
	failed=0
	sweep_start=$(date +%s)
	for env in $(all_envs); do
		echo "==> Building $(env_label "$env") [$env]"
		$FULLCLEAN && pio run -e "$env" -t fullclean >/dev/null
		start=$(date +%s)
		size=""
		if pio run -e "$env"; then
			status="PASS"
			bin=".pio/build/$env/firmware.bin"
			[[ -f "$bin" ]] && size="  $(( $(wc -c <"$bin") / 1024 )) KB"
		else
			status="FAIL"; failed=$((failed + 1))
		fi
		results+=("$(printf '%-4s  %-24s %s' "$status" "$(env_label "$env")" "$(format_duration $(( $(date +%s) - start )))")$size")
	done
	echo
	echo "Build summary"
	printf '  %s\n' "${results[@]}"
	echo "  $failed failed, total $(format_duration $(( $(date +%s) - sweep_start )))"
	exit $(( failed > 0 ))
fi

if [[ -z "$ENV_NAME" ]]; then
	mapfile_envs=()
	while IFS= read -r e; do mapfile_envs+=("$e"); done < <(all_envs)
	if [[ ! -t 0 ]]; then
		ENV_NAME="${mapfile_envs[0]}"
		echo "Non-interactive shell, using $(env_label "$ENV_NAME") [$ENV_NAME]"
	else
	echo "Select a device:"
	for i in "${!mapfile_envs[@]}"; do
		printf '  %d) %s\n' $((i + 1)) "$(env_label "${mapfile_envs[$i]}")"
	done
	read -rp "> " choice
	if ! [[ "$choice" =~ ^[0-9]+$ ]] || (( choice < 1 || choice > ${#mapfile_envs[@]} )); then
		echo "Invalid choice" >&2
		exit 1
	fi
	ENV_NAME="${mapfile_envs[$((choice - 1))]}"
	fi
fi

echo "==> $(env_label "$ENV_NAME") [$ENV_NAME]"
PORT_ARGS=()
[[ -n "$PORT" ]] && PORT_ARGS=(--upload-port "$PORT")

print_hashes() {
	local f
	for f in firmware.elf firmware.bin; do
		[[ -f ".pio/build/$ENV_NAME/$f" ]] &&
			echo "$f SHA256: $(shasum -a 256 ".pio/build/$ENV_NAME/$f" | awk '{print $1}')"
	done
	return 0
}

start=$(date +%s)
$FULLCLEAN && pio run -e "$ENV_NAME" -t fullclean
if $JUST_BUILD; then
	pio run -e "$ENV_NAME"
	echo "Built in $(format_duration $(( $(date +%s) - start )))"
	print_hashes
	exit
fi
UPLOAD_TARGET=upload
if $ERASE; then
	# The M9's UART bridge resets between esptool runs; erase and write in one session.
	if [[ "$ENV_NAME" == m9 ]]; then UPLOAD_TARGET=upload_erase
	else pio run -e "$ENV_NAME" -t erase ${PORT_ARGS[@]+"${PORT_ARGS[@]}"}; fi
fi
pio run -e "$ENV_NAME" -t "$UPLOAD_TARGET" ${PORT_ARGS[@]+"${PORT_ARGS[@]}"}
echo "Built and uploaded in $(format_duration $(( $(date +%s) - start )))"
print_hashes

if $MONITOR; then
	# Native-USB boards re-enumerate after the reset; give the port a moment.
	sleep 2
	MON_ARGS=()
	[[ -n "$PORT" ]] && MON_ARGS=(--port "$PORT")
	pio device monitor -e "$ENV_NAME" ${MON_ARGS[@]+"${MON_ARGS[@]}"}
fi

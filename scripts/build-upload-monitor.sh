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
#   -p, --port PORT        upload/monitor port (default: PlatformIO auto-detect)
#   -e, --erase            erase flash before uploading (wipes config)
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
		*)              echo "$1" ;;
	esac
}

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
	case "$1" in
		-9|--m9)             ENV_NAME="m9" ;;
		-w|--wio-tracker-l2) ENV_NAME="wio-tracker-l2" ;;
		-t|--tdeck-plus)     ENV_NAME="tdeck-plus" ;;
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
	for env in $(all_envs); do
		echo "==> Building $(env_label "$env") [$env]"
		$FULLCLEAN && pio run -e "$env" -t fullclean >/dev/null
		start=$(date +%s)
		if pio run -e "$env"; then status="PASS"; else status="FAIL"; failed=$((failed + 1)); fi
		results+=("$(printf '%-4s  %-24s %4ss' "$status" "$(env_label "$env")" "$(( $(date +%s) - start ))")")
	done
	echo
	echo "Build summary"
	printf '  %s\n' "${results[@]}"
	exit $(( failed > 0 ))
fi

if [[ -z "$ENV_NAME" ]]; then
	mapfile_envs=()
	while IFS= read -r e; do mapfile_envs+=("$e"); done < <(all_envs)
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

echo "==> $(env_label "$ENV_NAME") [$ENV_NAME]"
PORT_ARGS=()
[[ -n "$PORT" ]] && PORT_ARGS=(--upload-port "$PORT")

$FULLCLEAN && pio run -e "$ENV_NAME" -t fullclean
if $JUST_BUILD; then
	pio run -e "$ENV_NAME"
	exit
fi
$ERASE && pio run -e "$ENV_NAME" -t erase ${PORT_ARGS[@]+"${PORT_ARGS[@]}"}
pio run -e "$ENV_NAME" -t upload ${PORT_ARGS[@]+"${PORT_ARGS[@]}"}

if $MONITOR; then
	# Native-USB boards re-enumerate after the reset; give the port a moment.
	sleep 2
	MON_ARGS=()
	[[ -n "$PORT" ]] && MON_ARGS=(--port "$PORT")
	pio device monitor -e "$ENV_NAME" ${MON_ARGS[@]+"${MON_ARGS[@]}"}
fi

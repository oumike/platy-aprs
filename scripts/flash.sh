#!/bin/bash
# Flash platy-aprs to a connected board.
# Expects a merged factory image (bootloader + partitions + boot_app0 + app),
# such as the platy-aprs-<board>-vX.Y.Z.bin from a release. That layout is
# written at 0x0; for an app-only -ota.bin use
#   esptool.py write_flash 0x10000 <file>-ota.bin
#
# Usage: ./flash.sh [--erase|-E] <platy-aprs-<board>-vX.Y.Z.bin> [port]
#   --erase, -E   Erase the whole chip first (wipes the settings).
# Default port: /dev/ttyUSB0 (Linux); on macOS use /dev/cu.usbmodem* or
# /dev/cu.wchusbserial*.
#
# A plain flash leaves the settings partition alone, so settings survive.

set -e

ERASE=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --erase|-E) ERASE=1 ;;
        *)          ARGS+=("$arg") ;;
    esac
done

FIRMWARE=${ARGS[0]:-}
PORT=${ARGS[1]:-/dev/ttyUSB0}

if [[ -z "$FIRMWARE" ]]; then
    # Fall back to the only factory image in the current directory.
    FIRMWARE=$(ls platy-aprs-*.bin 2>/dev/null | grep -v -- '-ota\.bin$' | sort -V | tail -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Usage: ./flash.sh [--erase|-E] <platy-aprs-<board>-vX.Y.Z.bin> [port]"
    exit 1
fi
if [[ "$FIRMWARE" == *-ota.bin ]]; then
    echo "$FIRMWARE is an app-only image; flash it at 0x10000, or use the factory .bin." >&2
    exit 1
fi

if command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
else ESPTOOL=(python3 -m esptool); fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings will be lost)..."
    "${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
"${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"

#!/usr/bin/env bash
# Turns built environments into release images in dist/:
#   platy-aprs-<board>-<version>.bin      full image, flash at 0x0
#   platy-aprs-<board>-<version>-app.bin  app only, flash at 0x10000
# Usage: scripts/package.sh <version> [env...]   (default: all firmware envs)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

VERSION="${1:?usage: package.sh <version> [env...]}"
shift
ENVS=("$@")
[ ${#ENVS[@]} -gt 0 ] || ENVS=(m9 wio-tracker-l2 tdeck-plus)

BOOT_APP0="$(find "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages" \
    -path '*framework-arduinoespressif32*' -name boot_app0.bin 2>/dev/null | head -1)"
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found; build first" >&2; exit 1; }

if command -v esptool.py >/dev/null; then ESPTOOL=esptool.py
elif command -v esptool >/dev/null; then ESPTOOL=esptool
else ESPTOOL="python3 -m esptool"; fi

mkdir -p dist
for env in "${ENVS[@]}"; do
    d=".pio/build/$env"
    [ -f "$d/firmware.bin" ] || { echo "$env: not built" >&2; exit 1; }
    out="dist/platy-aprs-$env-$VERSION"
    $ESPTOOL --chip esp32s3 merge_bin -o "$out.bin" \
        --flash_mode qio --flash_freq 80m --flash_size 16MB \
        0x0 "$d/bootloader.bin" 0x8000 "$d/partitions.bin" \
        0xe000 "$BOOT_APP0" 0x10000 "$d/firmware.bin" >/dev/null
    cp "$d/firmware.bin" "$out-app.bin"
    echo "  $env -> $out.bin, $out-app.bin"
done

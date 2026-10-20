# platy-aprs

APRS client firmware. It talks KISS over Bluetooth LE to an external TNC or radio (Mobilinkd TNC3/TNC4, Vero VR-N76/VR-N7600, or any BLE KISS device with configurable UUIDs), or LoRa APRS on the board's built-in radio. It has an LVGL UI and a web config page.

## Supported boards

| Board | Env | Radio | Input |
|---|---|---|---|
| Elecrow ThinkNode M9 | `m9` | LR1110 | keypad |
| Seeed Wio Tracker L2 | `wio-tracker-l2` | SX1262 | touch, BOOT button (Back, hold = Home) |
| LilyGo T-Deck Plus | `tdeck-plus` | SX1262 | keyboard, trackball, touch |
| Attaky Mesh Deck | `mesh-deck` | SX1262 | keyboard module, touch, d-pad (Select = Enter, hold = Back) |

## Install

Download the image for your board from [Releases](../../releases) and flash it at 0x0, either with
`./flash.sh platy-aprs-<board>-<version>.bin [port]` (also attached to each release),
`esptool.py --chip esp32s3 write_flash 0x0 platy-aprs-<board>-<version>.bin`, or in the browser with
[esptool-js](https://espressif.github.io/esptool-js/). The `-ota.bin` images (app only, at 0x10000) update the firmware and keep your settings.

## Build & flash

```sh
scripts/build-upload-monitor.sh            # pick a board from a menu, build, flash, monitor
scripts/build-upload-monitor.sh -w -p /dev/cu.usbmodem101   # Wio Tracker L2 (-9 M9, -t T-Deck Plus, -m Mesh Deck)
scripts/build-upload-monitor.sh -B         # build every board, print a summary
pio test -e native                         # host-side unit tests
```

Other flags: `-e` erase flash (on the M9, erase and write in one esptool session via `-t upload_erase`),
`-c` clean build, `-d` debug build, `-n` don't open the monitor. It prints the build time and firmware SHA256s,
and in a non-interactive shell picks the first board instead of showing the menu.
## Releases

```sh
scripts/release.sh --dist-only                 # build release images for every board into dist/, release nothing
scripts/release.sh --dist-only --only m9,mesh-deck --version 0.2.0+test
scripts/release.sh --notes-only                # draft RELEASE_NOTES.md from the commits since the last tag
scripts/release.sh -y                          # release: bump the patch of the latest tag, dispatch the Release workflow
scripts/release.sh --version 0.2.0 [--alpha]   # an exact version; --alpha publishes vX.Y.Z-alpha.N as a prerelease
```

`dist/` holds, per board, `platy-aprs-<board>-vX.Y.Z.bin` (factory image, 0x0), `-ota.bin` (app only, 0x10000)
and `.elf` (symbols), plus `flash.sh`, `BUILD-INFO.txt` and `SHA256SUMS`. A dist-only build is versioned from
`VERSION`, with `+<commit>` (and `.custom` for uncommitted changes) unless it is the tagged release commit.

A release ships only committed work on a branch in sync with origin. The script picks the version and
dispatches `.github/workflows/release.yml`, which builds every board, commits `VERSION`, tags `v<version>` and
publishes the images with `RELEASE_NOTES.md` (or GitHub's generated notes). `--build-local` does all of that on
this machine instead. CI (`.github/workflows/build.yml`) builds the same images on each push and keeps them as
workflow artifacts. `scripts/release.sh --help` lists every option.

## First-time setup

1. If the device has no settings (new or erased flash) but the SD card has backups in `/platy/backups`, it first asks
   whether to restore one (it reboots with those settings) or start fresh.
2. On first boot the device opens an open access point named `PLATY-APRS-XXXX`. The home screen shows its name and a join QR code.
3. Join that network and browse to `http://192.168.4.1`. There is no captive portal.
4. Enter your callsign and other settings, then set your Wi-Fi network on the **Wi-Fi** tab and save.
   You can also join a network on the device: **Settings → Wi-Fi networks** scans, then pick a network and
   enter its password (**Other network...** for hidden ones).
5. Once the device joins your network, the setup AP shuts down after about 15 s. Config is then served at `http://<device-ip>` and `http://platy-aprs.local`.
6. If your network is unreachable for 60 s, the setup AP comes back. You can also reopen it from **Settings → Open setup access point** on the device while it is not connected to a network. The AP never stays up once the device is connected.

You can set an optional admin password (username `admin`) on the **Device** tab.

## Keys (Mesh Deck keyboard)

Letters, digits, `,` `.` and Space type as labelled; Shift gives capitals and `!@#$%^&*()` on the number row.
Hold **Sym** for punctuation: `q`–`p` → `? / - _ = + [ ] { }`, `a`–`l` → `: ; ' " `` ` `` ~ \ | &`,
`z`–`m` → `< > * @ # % ^`, `,` → `!`, `.` → `?`. Tab moves to the next field.
**Alt** chords: Alt+H home, Alt+M messages, Alt+P map, Alt+S stations, Alt+L monitor, Alt+Backspace back.
The on-screen keyboard is not used on this board.

## Keys (M9)

D-pad: move/select · Back: previous screen/close · Home: home screen · button below Home: monitor ·
button below Back: stations · Messages: messages · Map: map (station heat map).

The home screen shows a compact panel (last heard station, last received message, last packet, or nothing;
choose in Settings or the web Device tab). Connection details and the setup QR code are under
"Status & connection info"; that pops up automatically while the setup AP is on.

## Bluetooth TNC

Web **Radio** tab (or device **Settings > Radio link** screen): Scan, pick the radio, and it connects and reconnects automatically.
Defaults match the common BLE KISS service (`00000001-ba2a-46c9-ae49-01b0961f68bb`); UUIDs are configurable.
Put the radio in BLE pairing mode for the first connection (tested with the BTECH/Vero VR-N7600).
If receive works but sends fail (for example after a full erase), the pairing keys no longer match: delete the pairing under device **Settings > Paired radios** or the web **Radio** tab (**Paired radios**), remove the device from the radio's Bluetooth list too, then connect again.

## LoRa APRS

The RF link is either the BLE TNC **or** LoRa, never both. Switch it in the web **Radio** tab or with
**Use LoRa / Use BLE TNC** on the device's Radio link screen; the status bar shows `LoRa` instead of the Bluetooth icon.
Defaults follow the common LoRa APRS convention: 433.775 MHz, SF12, BW 125 kHz, CR 4/5, 20 dBm, with the
`<\xFF\x01` + TNC2 text framing, so it interoperates with LoRa APRS iGates/trackers (e.g. CA2RXU, APLRG1).
Frequency (150–960 MHz), SF, bandwidth, coding rate and power are configurable. The M9's antenna and RF path
are built for its sold band (433 or 868/915), so stick to that band. Transmitting needs an amateur licence.

Preproduction M9 LR1110s run old radio firmware that rejects one RadioLib setup command (`begin()` → -706);
`tools/patch_radiolib_lr11x0.py` patches RadioLib at build time to skip it.

## Beaconing & messaging

Position source (web **Beacon** tab): GPS (with SmartBeaconing or a fixed interval), a fixed position, or off.
Beacons go out as `CALL-SSID>APZRHN,<path>` once a callsign is set and the TNC (or APRS-IS) is connected; "Send beacon now"
is on the home screen and the web Beacon tab. The GPS also sets the clock when there is no NTP.

Messages (device **Messages** screen, the MESSAGE key, or the web **Messages** tab) are retried 5 times
(30 s, 1, 2, 4 min) until acked; incoming messages are acked, de-duplicated, beep, and pop up a banner.
Conversations are kept on the SD card (`/platy/messages.json`).

The list button left of the message input opens your five quick messages; picking one fills in the text.
Edit them under **Settings → Quick messages** on the device or on the web **Station** tab.

Stations and conversations can be favorited with the star on their detail screen (or in the web Stations/Messages tabs).
Favorites are listed first and are never dropped from the station list when it fills up.

## APRS-IS, iGate & digipeater

Web **APRS-IS** tab. With "Connect to APRS-IS" on, the device logs in (passcode computed from the callsign
unless set) whenever it is on Wi-Fi, receives packets for the server filter (default `m/50`), and sends its
own beacons and messages to APRS-IS as well as over RF, so messaging also works without a TNC or a local iGate.

- **RF → IS**: heard RF packets are gated with `qAR,<call>` (skips NOGATE/RFONLY/TCPIP/TCPXX, queries, and
  third-party packets that came from the Internet).
- **IS → RF messages** (off by default): messages for a station heard directly on RF in the last 30 min, from
  a sender not on RF, are sent as third-party traffic via the configured path.
- **Digipeater** (off by default): WIDE1-1 fill-in (`<call>*,WIDE1*`) and packets routed through our callsign,
  with 30 s duplicate suppression.

## Map

Home → **Map** (or the MAP key). OpenStreetMap tiles come from `maps.camillia.sumat.org` when on Wi-Fi and are
cached on the SD card (`/platy/tiles/z/x/y.png`), so areas you have viewed work offline. Stations with a
position are drawn as dots (amber RF, grey APRS-IS, blue weather, purple objects; "me" in blue) over a
heat map of every heard station: blobs merge from blue to red as stations cluster, so busy areas stand out
when zoomed out.

D-pad: pan · `+`/`i`: zoom in · `-`/`o`: zoom out · `N`: select the next station in view (and centre on it) ·
`C`: centre on me · Enter: details of the selected station.

## Packet log

With "Log packets to the SD card" on (web **Device** tab, default on), every RF packet and everything we send
is appended to `/platy/logs/YYYY-MM-DD.log` (UTC; `nodate.log` before the clock is set) as
`2026-10-12 21:54:03Z RF< SRC>DST,PATH:info` (`RF<` heard, `RF>` sent, `IS<`/`IS>` APRS-IS). APRS-IS
traffic is only logged if enabled. Logs can be downloaded or deleted from the Device tab.

## Backup & restore

Web **Device** tab: download a backup JSON file, restore one by uploading it, or save backups to the SD card
(`/platy/backups/`) and restore, download, or delete them there. Passwords are only included if you tick the box.
Restoring reboots the device. The device Settings screen can also save a backup (no passwords) to SD.

## API (selection)

Debug builds (`PLATFORMIO_BUILD_FLAGS=-DPLATY_SERIAL_KEYS`) also accept raw M9 key codes on the serial
console and serve a raw RGB565 screen dump at `GET /api/debug/screen`.


Every `POST` must carry an `X-Platy: 1` header (cross-site request guard); with an admin password set, all
endpoints need HTTP Basic auth as `admin`.

`GET /api/status` · `GET/POST /api/config` · `GET /api/tnc/status` · `GET /api/tnc/scan[?start=1]` ·
`POST /api/tnc/select|connect|disconnect` · `GET /api/packets?since=N` · `GET /api/stations` ·
`POST /api/beacon` · `GET /api/messages[?peer=]` · `POST /api/messages` (`{to,text}`) ·
`POST /api/messages/read|delete?peer=` · `POST /api/favorites/toggle?call=` · `POST /api/messages/retry?seq=` · `GET /api/backup[?secrets=1]` · `POST /api/restore` · `GET|POST /api/backup/sd` ·
`POST /api/backup/sd/restore?name=` · `GET /api/backup/sd/download?name=` · `POST /api/backup/sd/delete?name=` ·
`GET /api/logs` · `GET /api/logs/download?name=` · `POST /api/logs/delete?name=`

## Status

Milestones 1 (skeleton, display, Wi-Fi, web config), 2 (BLE KISS, APRS decode, monitor/stations, SD, backup/restore)
and 3 (GPS, beaconing/SmartBeaconing, messaging, buzzer alerts) are done, plus the APRS-IS iGate, digipeater
SD packet log, map and LoRa APRS (milestone 4). The T-Deck Plus and Mesh Deck builds have not been tested on hardware yet. The Mesh Deck has no SD slot, so map tiles are not cached and there is no packet log; after flashing it stays halted until power-cycled.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). Board pin maps and bring-up sequences follow camillia-mt, and the
Mesh Deck keyboard matrix layout comes from the [wadamesh](https://github.com/ALLFATHER-BV/wadamesh) MeshCore
port, both GPL-3.0-or-later.

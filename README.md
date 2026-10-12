# rhino-aprs

APRS client firmware. It talks KISS over Bluetooth LE to an external TNC or radio (Mobilinkd TNC3/TNC4, Vero VR-N76/VR-N7600, or any BLE KISS device with configurable UUIDs), or LoRa APRS on the M9's built-in LR1110. It has an LVGL UI and a web config page.

First target: **Elecrow ThinkNode M9** (ESP32-S3).

## Build & flash

```sh
pio run -e m9 -t upload --upload-port /dev/cu.wchusbserial10
pio device monitor -p /dev/cu.wchusbserial10
pio test -e native        # host-side unit tests
```

## First-time setup

1. On first boot the device opens an open access point named `RHINO-APRS-XXXX`. The home screen shows its name and a join QR code.
2. Join that network and browse to `http://192.168.4.1`. There is no captive portal.
3. Enter your callsign and other settings, then set your Wi-Fi network on the **Wi-Fi** tab and save.
4. Once the device joins your network, the setup AP shuts down after about 15 s. Config is then served at `http://<device-ip>` and `http://rhino-aprs.local`.
5. If your network is unreachable for 60 s, the setup AP comes back. You can also reopen it from **Settings → Open setup access point** on the device.

You can set an optional admin password (username `admin`) on the **Device** tab.

## Keys (M9)

D-pad: move/select · Back: previous screen/close · Home: home screen · button below Home: monitor ·
Messages: messages · Map: map.

The home screen shows a compact panel (last heard station, last received message, last packet, or nothing;
choose in Settings or the web Device tab). Connection details and the setup QR code are under
"Status & connection info"; that pops up automatically while the setup AP is on.

## Bluetooth TNC

Web **Radio** tab (or device **Settings > Radio link** screen): Scan, pick the radio, and it connects and reconnects automatically.
Defaults match the common BLE KISS service (`00000001-ba2a-46c9-ae49-01b0961f68bb`); UUIDs are configurable.
Put the radio in BLE pairing mode for the first connection (tested with the BTECH/Vero VR-N7600).

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
Conversations are kept on the SD card (`/rhino/messages.json`).

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
cached on the SD card (`/rhino/tiles/z/x/y.png`), so areas you have viewed work offline. Stations with a
position are drawn as dots (amber RF, grey APRS-IS, blue weather, purple objects; "me" in blue).

D-pad: pan · `+`/`i`: zoom in · `-`/`o`: zoom out · `N`: select the next station in view (and centre on it) ·
`C`: centre on me · Enter: details of the selected station.

## Packet log

With "Log packets to the SD card" on (web **Device** tab, default on), every RF packet and everything we send
is appended to `/rhino/logs/YYYY-MM-DD.log` (UTC; `nodate.log` before the clock is set) as
`2026-10-12 21:54:03Z RF< SRC>DST,PATH:info` (`RF<` heard, `RF>` sent, `IS<`/`IS>` APRS-IS). APRS-IS
traffic is only logged if enabled. Logs can be downloaded or deleted from the Device tab.

## Backup & restore

Web **Device** tab: download a backup JSON file, restore one by uploading it, or save backups to the SD card
(`/rhino/backups/`) and restore, download, or delete them there. Passwords are only included if you tick the box.
Restoring reboots the device. The device Settings screen can also save a backup (no passwords) to SD.

## API (selection)

Debug builds (`PLATFORMIO_BUILD_FLAGS=-DRHINO_SERIAL_KEYS`) also accept raw M9 key codes on the serial
console and serve a raw RGB565 screen dump at `GET /api/debug/screen`.


`GET /api/status` · `GET/POST /api/config` · `GET /api/tnc/status` · `GET /api/tnc/scan[?start=1]` ·
`POST /api/tnc/select|connect|disconnect` · `GET /api/packets?since=N` · `GET /api/stations` ·
`POST /api/beacon` · `GET /api/messages[?peer=]` · `POST /api/messages` (`{to,text}`) ·
`POST /api/messages/read|delete?peer=` · `POST /api/messages/retry?seq=` · `GET /api/backup[?secrets=1]` · `POST /api/restore` · `GET|POST /api/backup/sd` ·
`POST /api/backup/sd/restore?name=` · `GET /api/backup/sd/download?name=` · `POST /api/backup/sd/delete?name=` ·
`GET /api/logs` · `GET /api/logs/download?name=` · `POST /api/logs/delete?name=`

## Status

Milestones 1 (skeleton, display, Wi-Fi, web config), 2 (BLE KISS, APRS decode, monitor/stations, SD, backup/restore)
and 3 (GPS, beaconing/SmartBeaconing, messaging, buzzer alerts) are done, plus the APRS-IS iGate, digipeater
SD packet log, map and LoRa APRS (milestone 4).

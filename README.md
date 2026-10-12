# rhino-aprs

APRS client firmware. It talks KISS over Bluetooth LE to an external TNC or radio (Mobilinkd TNC3/TNC4, Vero VR-N76/VR-N7600, or any BLE KISS device with configurable UUIDs). It has an LVGL UI and a web config page.

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

D-pad: move/select · Back: previous screen/close · Home: home screen.

The home screen shows a compact panel (last heard station, last received message, last packet, or nothing;
choose in Settings or the web Device tab). Connection details and the setup QR code are under
"Status & connection info"; that pops up automatically while the setup AP is on.

## Bluetooth TNC

Web **TNC** tab (or device **TNC** screen): Scan, pick the radio, and it connects and reconnects automatically.
Defaults match the common BLE KISS service (`00000001-ba2a-46c9-ae49-01b0961f68bb`); UUIDs are configurable.
Put the radio in BLE pairing mode for the first connection (tested with the BTECH/Vero VR-N7600).

## Beaconing & messaging

Position source (web **Beacon** tab): GPS (with SmartBeaconing or a fixed interval), a fixed position, or off.
Beacons go out as `CALL-SSID>APZRHN,<path>` once a callsign is set and the TNC is connected; "Send beacon now"
is on the home screen and the web Beacon tab. The GPS also sets the clock when there is no NTP.

Messages (device **Messages** screen, the MESSAGE key, or the web **Messages** tab) are retried 5 times
(30 s, 1, 2, 4 min) until acked; incoming messages are acked, de-duplicated, beep, and pop up a banner.
Conversations are kept on the SD card (`/rhino/messages.json`).

## Backup & restore

Web **Device** tab: download a backup JSON file, restore one by uploading it, or save backups to the SD card
(`/rhino/backups/`) and restore, download, or delete them there. Passwords are only included if you tick the box.
Restoring reboots the device. The device Settings screen can also save a backup (no passwords) to SD.

## API (selection)

`GET /api/status` · `GET/POST /api/config` · `GET /api/tnc/status` · `GET /api/tnc/scan[?start=1]` ·
`POST /api/tnc/select|connect|disconnect` · `GET /api/packets?since=N` · `GET /api/stations` ·
`POST /api/beacon` · `GET /api/messages[?peer=]` · `POST /api/messages` (`{to,text}`) ·
`POST /api/messages/read|delete?peer=` · `POST /api/messages/retry?seq=` · `GET /api/backup[?secrets=1]` · `POST /api/restore` · `GET|POST /api/backup/sd` ·
`POST /api/backup/sd/restore?name=` · `GET /api/backup/sd/download?name=` · `POST /api/backup/sd/delete?name=`

## Status

Milestones 1 (skeleton, display, Wi-Fi, web config), 2 (BLE KISS, APRS decode, monitor/stations, SD, backup/restore)
and 3 (GPS, beaconing/SmartBeaconing, messaging, buzzer alerts) are done. Next: APRS-IS iGate, digipeater,
SD packet logging, map, OTA.

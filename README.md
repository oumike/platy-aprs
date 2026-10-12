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

D-pad: move/select · Back: previous screen · Home: home screen.

## Status

Milestone 1 (skeleton, display, keyboard, config, Wi-Fi, web config) is done. Next: BLE KISS + APRS decode.

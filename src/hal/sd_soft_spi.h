#pragma once
// Bit-banged SPI-mode SD card, mounted through FatFs and the ESP-IDF VFS so it
// presents as an ordinary fs::FS. From camillia-mt src/sd_soft_spi.*.
//
// For boards whose card slot sits on pins neither hardware SPI host can serve.
// The CrowPanel Advance 3.5 is the case: its display holds SPI3, the LoRa radio
// holds SPI2 (Arduino's global SPI), and the S3 has no third general-purpose
// host. Meshtastic drives the same slot the same way (SDCARD_USE_SOFT_SPI).
//
// Pins come from the board header: PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI and
// PIN_SD_CS. All must be below GPIO32 -- the driver writes the low GPIO register
// directly.
#include <Arduino.h>
#include <FS.h>

// Probes the card and mounts it at /sdcard. False when no card answers or it has
// no readable FAT filesystem -- a card is never formatted from here.
bool sdSoftBegin();

// Unmounts and releases the drive. Safe when nothing is mounted.
void sdSoftEnd();

bool sdSoftMounted();

// The mounted card. Operations fail cleanly while it is unmounted.
fs::FS &sdSoftFs();

// Card capacity in bytes from its CSD; 0 when unmounted.
uint64_t sdSoftCardSize();

// Bytes in use on the mounted FAT volume; 0 when unmounted or unreadable.
uint64_t sdSoftUsedBytes();

// "SDHC", "SDSC" or "MMC"; empty when unmounted.
const char *sdSoftCardTypeName();

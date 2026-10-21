#pragma once
#include <Arduino.h>
#include <FS.h>

// microSD: SPI on the M9 and T-Deck Plus (shared with the display and radio),
// 1-bit SDMMC on the Wio Tracker L2, none on the Mesh Deck. Access only from
// the loop task (the display flush runs there too, so the bus is never
// contended).
//
// A card that is missing at boot, slow to answer, or inserted later is picked
// up by sdLoop(), which retries with a backoff as camillia-mt does (2, 5, 15,
// 30, then every 60 s).
bool sdBegin();
void sdLoop();
bool sdHasSlot();    // false on boards without a card slot
bool sdMounted();
// Mount now if not mounted (e.g. right before a backup), ignoring the backoff
// but not more often than every few seconds. Cheap when already mounted.
bool sdRemount();
uint32_t sdMountCount();     // bumps on every successful mount
uint64_t sdTotalBytes();
uint64_t sdUsedBytes();
String sdStatusText();       // "7.4 GB card, 3% used", "no card (retry in 15 s)", "no slot"
fs::FS &sdFs();
// mkdir -p
bool sdMakeDirs(const char *path);

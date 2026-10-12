#pragma once
#include <Arduino.h>
#include <FS.h>

// microSD on the shared display SPI bus. Access only from the loop task (the
// display flush runs there too, so the bus is never contended).
bool sdBegin();
bool sdMounted();
// Retry mounting (e.g. after a card was inserted). Cheap when already mounted.
bool sdRemount();
uint64_t sdTotalBytes();
uint64_t sdUsedBytes();
fs::FS &sdFs();
// mkdir -p
bool sdMakeDirs(const char *path);

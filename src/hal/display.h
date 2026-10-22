#pragma once
#include <stdint.h>

// LVGL 9 display driver: LovyanGFX panels (display.cpp) or e-paper
// (display_eink.cpp, HAS_EINK).
void displayBegin();
void displaySetBrightness(uint8_t level);   // 0..255
void displaySleep(bool sleep);              // panel SLPIN/SLPOUT + backlight
void displayLoop();                         // calls lv_timer_handler()
// A new screen is up. E-paper takes a full refresh to clear ghosting; no-op elsewhere.
void displayScreenChanged();
bool displayCanRotate();
// Rotates the panel (and touch) and resizes LVGL; the UI must be rebuilt after.
void displaySetOrientation(uint8_t o);      // Orientation: 0 landscape, 1 landscape 180, 2/3 portrait

#pragma once
#include <stdint.h>

// LovyanGFX panel + LVGL 9 display driver.
void displayBegin();
void displaySetBrightness(uint8_t level);   // 0..255
void displaySleep(bool sleep);              // panel SLPIN/SLPOUT + backlight
void displayLoop();                         // calls lv_timer_handler()
bool displayCanRotate();
// Rotates the panel (and touch) and resizes LVGL; the UI must be rebuilt after.
void displaySetOrientation(uint8_t o);      // Orientation: 0 landscape, 1 landscape 180, 2/3 portrait

#pragma once
#include <stdint.h>

// LovyanGFX panel + LVGL 9 display driver.
void displayBegin();
void displaySetBrightness(uint8_t level);   // 0..255
void displayLoop();                         // calls lv_timer_handler()

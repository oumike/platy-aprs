#pragma once
#include <stdint.h>
#include <lvgl.h>

// M9 I2C keyboard + d-pad, exposed to LVGL as a keypad input device.
void keyboardBegin();
void keyboardPoll();   // call from the UI loop

// Hotkeys (HOME, BACK, MESSAGE, MAP …) are offered to this handler first;
// returning true consumes the key so LVGL never sees it.
typedef bool (*KeyboardHotkeyHandler)(uint8_t key);
void keyboardSetHotkeyHandler(KeyboardHotkeyHandler h);

// The LVGL keypad device; attach a group to it with lv_indev_set_group().
lv_indev_t *keyboardIndev();

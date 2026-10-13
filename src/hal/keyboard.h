#pragma once
#include <stdint.h>
#include <lvgl.h>

// Board keys (M9 keyboard + d-pad, T-Deck keyboard + trackball, Wio buttons),
// exposed to LVGL as a keypad input device using the codes in keys.h.
void keyboardBegin();
void keyboardPoll();   // call from the UI loop

// Hotkeys (HOME, BACK, MESSAGE, MAP …) are offered to this handler first;
// returning true consumes the key so LVGL never sees it.
typedef bool (*KeyboardHotkeyHandler)(uint8_t key);
void keyboardSetHotkeyHandler(KeyboardHotkeyHandler h);

// The LVGL keypad device; attach a group to it with lv_indev_set_group().
lv_indev_t *keyboardIndev();

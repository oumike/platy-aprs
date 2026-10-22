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

// E-paper builds: run while the panel refresh blocks, so keystrokes queue up
// instead of overflowing the keyboard controller. Touches no LVGL state.
void keyboardServiceDuringRefresh();
uint32_t keyboardLastInputMs();   // millis() of the last key that reached the UI

// Keyboard backlight (T-Deck, T-LoRa Pager, T-Deck Pro), 0 = off .. 255 = full (the Pager is on/off).
void keyboardSetBacklight(uint8_t duty);

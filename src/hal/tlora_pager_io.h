#pragma once
#include <stdint.h>
// LilyGo T-LoRa Pager I2C peripherals on the main Wire bus: XL9555 rail
// expander, TCA8418 keyboard and BQ25896 charger (camillia-mt).
bool pagerIoBegin();                 // Wire up, rails on (amp, haptics and NFC off)
bool pagerIoSet(uint8_t bit, bool high);
uint16_t pagerBatteryMv();           // 0 until the charger has answered
bool pagerKeyboardBegin();           // matrix setup; true when the controller answers
// Drains the controller's event FIFO and writes the keys pressed (ASCII or
// keys.h codes, modifiers applied) to out; returns how many.
int pagerKeyboardScan(uint8_t *out, int max);

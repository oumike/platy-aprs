#pragma once
#include <stdint.h>
// LilyGo T-Deck Pro I2C peripherals on the main Wire bus: TCA8418 keyboard,
// BQ25896 charger and BQ27220 fuel gauge (camillia-mt, wadamesh).
void tdeckProIoBegin();              // Wire up
uint16_t tdeckProBatteryMv();        // 0 until the charger has answered
int tdeckProBatteryPercent();        // BQ27220 state of charge, -1 if it doesn't answer
bool tdeckProKeyboardBegin();        // matrix setup; true when the controller answers
// Drains the controller's event FIFO and writes the keys pressed (ASCII or
// keys.h codes, modifiers and Alt chords applied) to out; returns how many.
int tdeckProKeyboardScan(uint8_t *out, int max);

#pragma once
#include <stdint.h>
// Attaky Mesh Deck I2C peripherals: AW9523 expander 0x59 (d-pad, FT6636 reset),
// FT6636 touch, MAX17048 fuel gauge and the keyboard module's two AW9523s
// (0x5A left half, 0x5B right half), all on the main Wire bus.
bool meshDeckIoBegin();                        // bus recovery, Wire up, touch out of reset
bool meshDeckReadButtons(uint8_t &port0);      // raw active-low d-pad port; false on a bad read
bool meshDeckReadTouch(int16_t &x, int16_t &y);  // native portrait coords; false when not touched
uint16_t meshDeckBatteryMv();                  // 0 when the gauge doesn't answer
int meshDeckBatteryPercent();                  // -1 when the gauge doesn't answer
bool meshDeckKeyboardBegin();                  // probes both halves (again); true when either answers
bool meshDeckKeyboardPresent();
// Scans the matrix and writes the keys pressed since the last scan to out (ASCII
// or keys.h codes, modifiers applied); returns how many.
int meshDeckKeyboardScan(uint8_t *out, int max);

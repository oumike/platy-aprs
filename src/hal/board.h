#pragma once
#include <stdint.h>

// Board bring-up: power rails, shared-SPI chip selects, buzzer, battery.
void boardBegin();
uint16_t boardBatteryMilliVolts();   // 0 when no valid sample yet
int boardBatteryPercent();           // -1 when unknown
void boardBeep(uint16_t freqHz, uint16_t ms);

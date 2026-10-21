#pragma once
#include <stdint.h>

// Board bring-up: power rails, shared-SPI chip selects, battery. Sound is in audio.h.
void boardBegin();
uint16_t boardBatteryMilliVolts();   // 0 when no valid sample yet
int boardBatteryPercent();           // -1 when unknown
const char *boardName();

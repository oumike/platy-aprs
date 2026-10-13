#pragma once
#include <stdint.h>
// Wio Tracker L2 PCA9555 expander: rails, resets and the wake button.
bool wioIoBegin();   // Wire up, safe state, LCD/touch/GNSS reset sequence
bool wioIoReady();
bool wioIoSet(uint8_t bit, bool high);
bool wioIoWakePressed();

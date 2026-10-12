#pragma once
#include <stdint.h>

// APRS-IS passcode for a callsign; the SSID suffix ("-9") is ignored and the
// base callsign is uppercased. Returns 0..32767, or -1 for an empty callsign.
int aprsPasscode(const char *callsign);

#pragma once
#include <Arduino.h>

// Position beaconing: fixed position on an interval, or GPS with an interval
// or SmartBeaconing. Loop task only.
struct BeaconStatus {
    const char *mode;      // "off" / "fixed" / "gps" / "smart"
    bool sentAny;
    uint32_t lastAgoS;     // since the last beacon (valid if sentAny)
    int32_t nextInS;       // -1 when unknown / waiting
    String waitingFor;     // why nothing goes out ("" when ready)
    String lastError;      // last failed beacon attempt
};

void beaconLoop();
bool beaconNow(String &err);   // sends a position right now
BeaconStatus beaconStatus();

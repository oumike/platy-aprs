#pragma once
#include <Arduino.h>

// CC1167Q GPS on UART1 (NMEA, 115200).
struct GpsFix {
    bool enabled;        // receiver powered
    bool alive;          // NMEA seen in the last 5 s
    bool valid;          // position fix younger than 5 s
    double lat, lon;
    double altM;
    bool hasAlt;
    double speedKmh;
    int course;          // degrees, only meaningful when moving
    uint8_t sats;
    double hdop;
    uint32_t fixAgeMs;
};

void gpsBegin();
void gpsLoop();                 // call from loop()
void gpsSetEnabled(bool on);    // powers the receiver up/down
GpsFix gpsFix();

#pragma once
#include <Arduino.h>

// Drains received frames from the transport(s), decodes them and feeds the
// packet log and station database. Call from loop().
void routerBegin();
void routerLoop();

// Our own position (fixed-position config for now; GPS in a later milestone).
bool myPosition(double &lat, double &lon);

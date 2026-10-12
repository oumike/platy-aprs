#pragma once
#include <Arduino.h>

// Drains received frames from the transport(s), decodes them and feeds the
// packet log and station database. Call from loop().
void routerBegin();
void routerLoop();

// Our own position: the GPS fix when there is one, else the fixed position
// from the config (if set).
bool myPosition(double &lat, double &lon);

// Transmit an APRS info field from our callsign via the configured path.
// Logged to the packet log as TX. Fails (with a reason) when the callsign is
// not set or the TNC is not connected.
bool routerSendInfo(const std::string &info, String &err);

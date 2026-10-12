#pragma once
#include <Arduino.h>
#include <string>

// Drains received frames from the transport(s) and APRS-IS, decodes them and
// feeds the packet log, station database and messages. Also runs the iGate
// and digipeater. Call from loop().
void routerBegin();
void routerLoop();

// One line received from APRS-IS (called by aprsIsLoop()).
void routerOnIsLine(const std::string &line);

// Our own position: the GPS fix when there is one, else the fixed position
// from the config (if set).
bool myPosition(double &lat, double &lon);

// True when routerSendInfo() has somewhere to send (TNC or verified APRS-IS).
bool routerCanSend();

// Transmit an APRS info field from our callsign: over RF via the configured
// path when the TNC is connected, and to APRS-IS when logged in. Logged to the
// packet log as TX. Fails (with a reason) when the callsign is not set or
// neither is available.
bool routerSendInfo(const std::string &info, String &err);

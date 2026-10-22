#pragma once
#include "config/config.h"

// New-message light on boards with an RGB status LED (the Mesh Deck): a double
// blink every second in g_config.ledColor while messages are unread, for at
// most g_config.ledTimeoutSecs after the last one arrived (0 = until read).
bool notifyLedPresent();
void notifyLedMessage();               // a new message arrived
void notifyLedPreview(LedColor c);     // one double blink, e.g. from Settings
void notifyLedLoop();                  // call from loop()
const char *ledColorLabel(LedColor c);   // "Blue", ...
String ledTimeoutLabel(uint16_t secs);   // "15 s", "1 min", "Never"

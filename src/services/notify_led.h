#pragma once
#include "config/config.h"

// New-message light: a double blink every second while messages are unread,
// for at most g_config.ledTimeoutSecs after the last one arrived (0 = until
// read). The Mesh Deck blinks its RGB LEDs in g_config.ledColor; the T-Deck
// blinks its keyboard backlight (g_config.kbBlink), only while the screen is
// off, and stops for good once the screen comes on (camillia-mt's behaviour).
bool notifyLedPresent();               // RGB LED with a colour setting
bool notifyKbBlinkPresent();           // keyboard backlight blink
inline bool notifyLightPresent() { return notifyLedPresent() || notifyKbBlinkPresent(); }
void notifyLedMessage();               // a new message arrived
void notifyLedPreview(LedColor c);     // one double blink, e.g. from Settings
void notifyLedApplySetting();          // after the colour or kbBlink changes
void notifyLedLoop();                  // call from loop()
const char *ledColorLabel(LedColor c);   // "Blue", ...
String ledTimeoutLabel(uint16_t secs);   // "15 s", "1 min", "Never"

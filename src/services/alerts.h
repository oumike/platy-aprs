#pragma once
#include "config/config.h"

// Alert tones, played without blocking. Silent when the sound is Off or the
// board has no speaker or buzzer.
enum class Alert { Message, Ack, Failed, Beacon, Boot };
void alertPlay(Alert a);
// The new-message tone in the given style at the configured volume, even when
// the saved style is Off (used by the settings screens). Off plays nothing.
void alertPreview(AlertSound style);
const char *alertSoundLabel(AlertSound s);   // "Default", "Chirpy", ...
void alertsLoop();   // call from loop()

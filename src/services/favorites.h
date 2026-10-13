#pragma once
#include <Arduino.h>

// Favorite callsigns, shared by the station list and conversations.
// Persisted in the config (and so in backups). Loop task only.

bool favIs(const char *call);
// Toggles and saves; returns the new state.
bool favToggle(const char *call);

// Bumped on every change, so lists can rebuild.
extern uint32_t g_favGen;

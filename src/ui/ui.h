#pragma once
#include <lvgl.h>

// Boot splash: shown straight after the display comes up, and kept for at
// least 1.5 s; uiBegin() replaces it with the home screen.
void uiShowSplash();
// The splash as a new, unloaded screen, with `footer` (if not null) along the bottom.
lv_obj_t *uiBuildSplash(const char *footer);
void uiBegin();
void uiLoop();

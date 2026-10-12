#pragma once
#include <lvgl.h>

// Slippy map with APRS stations. Tiles come from the SD card cache
// (/rhino/tiles/z/x/y.png) or, on Wi-Fi, the tile proxy (and are then cached).
// Fills `parent` and adds one focusable object to `grp`.
//   arrows pan · +/i zoom in · -/o zoom out · N next station · C center on me
//   Enter: details of the selected station (or center on me)
// `onStation` is called from an LVGL event; defer screen changes if needed.
void mapViewCreate(lv_obj_t *parent, lv_group_t *grp, void (*onStation)(const char *call));

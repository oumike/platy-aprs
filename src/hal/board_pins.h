#pragma once
// Selects the pin map for the build target. Each map also defines the
// capability flags: BOARD_NAME, HAS_TOUCH, HAS_KEYBOARD, HAS_BUZZER (piezo),
// LORA_CHIP_LR1110 / LORA_CHIP_SX1262, SD_USE_MMC, and optionally
// HAS_SD_CARD (default 1), HAS_I2S_AUDIO and HAS_ES8311 (default 0).
#if defined(DEVICE_M9)
#include "pins_m9.h"
#elif defined(DEVICE_WIO_TRACKER_L2)
#include "pins_wio_l2.h"
#elif defined(DEVICE_TDECK)
#include "pins_tdeck.h"
#elif defined(DEVICE_MESH_DECK)
#include "pins_mesh_deck.h"
#else
#error "No board selected: define DEVICE_M9, DEVICE_WIO_TRACKER_L2, DEVICE_TDECK or DEVICE_MESH_DECK"
#endif
#ifndef HAS_SD_CARD
#define HAS_SD_CARD 1
#endif
#ifndef HAS_I2S_AUDIO
#define HAS_I2S_AUDIO 0
#endif
#ifndef HAS_ES8311
#define HAS_ES8311 0
#endif

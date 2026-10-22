#pragma once
// Selects the pin map for the build target. Each map also defines the
// capability flags: BOARD_NAME, HAS_TOUCH, HAS_KEYBOARD, HAS_BUZZER (piezo),
// LORA_CHIP_LR1110 / LORA_CHIP_SX1262, SD_USE_MMC, and optionally
// HAS_SD_CARD, HAS_BATTERY and HAS_WAKE_BUTTON (default 1), SD_USE_SOFT_SPI,
// HAS_I2S_AUDIO, HAS_ES8311, HAS_NOTIFY_LED, HAS_KB_BLINK, HAS_ROTATION and HAS_EINK (default 0).
#if defined(DEVICE_M9)
#include "pins_m9.h"
#elif defined(DEVICE_WIO_TRACKER_L2)
#include "pins_wio_l2.h"
#elif defined(DEVICE_TDECK)
#include "pins_tdeck.h"
#elif defined(DEVICE_MESH_DECK)
#include "pins_mesh_deck.h"
#elif defined(DEVICE_CROWPANEL_35)
#include "pins_crowpanel_35.h"
#elif defined(DEVICE_CROWPANEL_50)
#include "pins_crowpanel_50.h"
#elif defined(DEVICE_TLORA_PAGER)
#include "pins_tlora_pager.h"
#elif defined(DEVICE_TDECK_PRO)
#include "pins_tdeck_pro.h"
#else
#error "No board selected: define DEVICE_M9, DEVICE_WIO_TRACKER_L2, DEVICE_TDECK, DEVICE_MESH_DECK, DEVICE_CROWPANEL_35, DEVICE_CROWPANEL_50, DEVICE_TLORA_PAGER or DEVICE_TDECK_PRO"
#endif
#ifndef HAS_SD_CARD
#define HAS_SD_CARD 1
#endif
#ifndef HAS_BATTERY
#define HAS_BATTERY 1
#endif
#ifndef HAS_WAKE_BUTTON
#define HAS_WAKE_BUTTON 1
#endif
#ifndef SD_USE_SOFT_SPI
#define SD_USE_SOFT_SPI 0
#endif
#ifndef HAS_KB_BLINK
#define HAS_KB_BLINK 0
#endif
#ifndef HAS_NOTIFY_LED
#define HAS_NOTIFY_LED 0
#endif
#ifndef HAS_I2S_AUDIO
#define HAS_I2S_AUDIO 0
#endif
#ifndef HAS_ES8311
#define HAS_ES8311 0
#endif
#ifndef HAS_ROTATION
#define HAS_ROTATION 0   // user-selectable screen orientation
#endif
#ifndef HAS_EINK
#define HAS_EINK 0       // 1-bit e-paper panel: monochrome UI, no animations
#endif

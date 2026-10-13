#pragma once
// Selects the pin map for the build target. Each map also defines the
// capability flags: BOARD_NAME, HAS_TOUCH, HAS_KEYBOARD, HAS_BUZZER,
// LORA_CHIP_LR1110 / LORA_CHIP_SX1262 and SD_USE_MMC.
#if defined(DEVICE_M9)
#include "pins_m9.h"
#elif defined(DEVICE_WIO_TRACKER_L2)
#include "pins_wio_l2.h"
#elif defined(DEVICE_TDECK)
#include "pins_tdeck.h"
#else
#error "No board selected: define DEVICE_M9, DEVICE_WIO_TRACKER_L2 or DEVICE_TDECK"
#endif

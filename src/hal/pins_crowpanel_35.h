#pragma once
// Elecrow CrowPanel Advance 3.5" HMI — ESP32-S3-WROOM-1-N16R8 (16 MB flash,
// 8 MB OPI PSRAM), 3.5" ILI9488 320x480 run as 480x320 landscape, GT911 touch,
// SX1262 module in the J8/J9 expansion slot, microSD over software SPI.
// No keyboard, no free user button (BOOT/GPIO0 is the LoRa NSS), no battery
// sense. Source: camillia-mt src/hal/hw_crowpanel_35.h (Meshtastic's
// elecrow_panel variant and Elecrow's V1.0 schematic).

#define BOARD_NAME         "CrowPanel 3.5"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       0
#define HAS_BUZZER         0    // GPIO8 is the I2S amp's MCLK; not driven
#define HAS_BATTERY        0
#define HAS_WAKE_BUTTON    0    // double tap wakes the screen from off as well
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0
#define SD_USE_SOFT_SPI    1

// ILI9488 on SPI3, write-only (the radio has SPI2 through Arduino's SPI).
#define PIN_TFT_SCK        42
#define PIN_TFT_MOSI       39
#define PIN_TFT_CS         40
#define PIN_TFT_DC         41
#define PIN_TFT_BL         38   // active HIGH
#define TFT_PANEL_W        320
#define TFT_PANEL_H        480
#define TFT_ROTATION       3    // 1 is upside down on the hardware (camillia-mt)
#define TFT_BL_PWM_CH      7

// GT911 touch on its own I2C pins.
#define PIN_I2C_SDA        15
#define PIN_I2C_SCL        16
#define I2C_FREQ           400000
#define PIN_TOUCH_INT      47
#define PIN_TOUCH_RST      48
#define TOUCH_ADDR         0x5D

// SX1262 in the expansion slot: TCXO on DIO3 at 3.3 V, DIO2 drives the RF switch.
#define PIN_LORA_SCK       10
#define PIN_LORA_MISO      9
#define PIN_LORA_MOSI      3
#define PIN_LORA_CS        0
#define PIN_LORA_RST       2
#define PIN_LORA_DIO1      1
#define PIN_LORA_BUSY      46
#define LORA_TCXO_V        3.3f
// IO45 routes the slot: LOW to the LoRa module, HIGH to the microphone.
#define PIN_SLOT_SELECT    45

// microSD (J5), bit-banged: both SPI hosts are taken. Pins must be < GPIO32.
#define PIN_SD_SCK         5
#define PIN_SD_MISO        4
#define PIN_SD_MOSI        6
#define PIN_SD_CS          7

// Optional GPS on the UART1 connector; nothing ships attached.
#define PIN_GPS_RX         18
#define PIN_GPS_TX         17
#define GPS_BAUD           9600

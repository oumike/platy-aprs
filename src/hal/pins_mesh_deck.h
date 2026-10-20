#pragma once
// Attaky Mesh Deck 1.0 — ESP32-S3-WROOM-1U-N16R8, 2.0" ST7789 240x320 on its
// own SPI bus, FT6636 touch, AW9523 expander (d-pad, touch reset, RGB LED),
// Ra-01SH SX1262 (crystal, no TCXO), ATGM336H GPS, MAX17048 fuel gauge, no SD,
// 48-key keyboard module (two AW9523 matrices).
// Source: camillia-mt src/hal/hw_mesh_deck.h (pins hardware-verified there).

#define BOARD_NAME         "Mesh Deck"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       1    // keyboard module: no on-screen keyboard
#define HAS_BUZZER         0
#define HAS_SD_CARD        0
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0

// ST7789 on SPI3 (the radio owns the Arduino SPI object on SPI2). Write-only.
#define PIN_TFT_SCK        11
#define PIN_TFT_MOSI       13
#define PIN_TFT_CS         45
#define PIN_TFT_DC         12
#define PIN_TFT_RST        10
#define PIN_TFT_BL         14   // active HIGH
#define TFT_PANEL_W        240
#define TFT_PANEL_H        320
#define TFT_ROTATION       3    // panel is mounted 180 degrees from the usual landscape
#define TFT_BL_PWM_CH      7

// Ra-01SH (SX1262) on its own SPI pins.
#define PIN_LORA_SCK       5
#define PIN_LORA_MISO      7
#define PIN_LORA_MOSI      6
#define PIN_LORA_CS        8
#define PIN_LORA_RST       9
#define PIN_LORA_DIO1      21
#define PIN_LORA_BUSY      16
#define LORA_TCXO_V        0.0f   // plain crystal: DIO3 must not power a TCXO

// Main I2C: touch, AW9523 expanders, fuel gauge.
#define PIN_I2C_SDA        2
#define PIN_I2C_SCL        1
#define I2C_FREQ           100000
#define TOUCH_ADDR         0x38   // FT6636, polled (its INT is on an expander)
#define MD_EXPANDER_ADDR   0x59   // AW9523: buttons on port 0, touch reset P13, LED P10-P12
#define FUEL_GAUGE_ADDR    0x36   // MAX17048
#define MD_KB_LEFT_ADDR    0x5A   // keyboard module, left half
#define MD_KB_RIGHT_ADDR   0x5B   // keyboard module, right half

// D-pad bits on expander port 0, active LOW.
#define MD_BTN_DOWN        0
#define MD_BTN_LEFT        1
#define MD_BTN_SELECT      2
#define MD_BTN_RIGHT       3
#define MD_BTN_UP          4

// ATGM336H GPS (host-side RX/TX), always powered by the frame.
#define PIN_GPS_RX         17
#define PIN_GPS_TX         18
#define GPS_BAUD           9600

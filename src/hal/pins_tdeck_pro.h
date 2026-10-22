#pragma once
// LilyGo T-Deck Pro — ESP32-S3, 16 MB flash, 8 MB PSRAM, 3.1" GDEQ031T10
// 240x320 monochrome e-paper (portrait) with a PWM frontlight, CST328 or
// CST3530 touch, TCA8418 keyboard, SX1262, u-blox MIA-M10Q GPS, BQ25896
// charger + BQ27220 gauge, microSD. No speaker or buzzer.
// Source: camillia-mt src/hal/hw_tdeck_pro.h, cross-checked with wadamesh.

#define BOARD_NAME         "T-Deck Pro"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       1
#define HAS_KB_BLINK       1    // keyboard backlight blinks for unread messages, screen off
#define HAS_BUZZER         0
#define HAS_EINK           1
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0

// Shared SPI bus: e-paper + SX1262 + microSD.
#define PIN_SPI_SCK        36
#define PIN_SPI_MISO       47
#define PIN_SPI_MOSI       33
#define PIN_TFT_CS         34   // e-paper
#define PIN_TFT_DC         35
#define PIN_TFT_RST        16
#define PIN_EINK_BUSY      37
#define PIN_TFT_BL         45   // frontlight, active high
#define TFT_BL_PWM_CH      0
#define TFT_BL_FREQ        12000
#define TFT_PANEL_W        240
#define TFT_PANEL_H        320
#define EINK_SPI_HZ        2000000
#define PIN_SD_CS          48

#define PIN_LORA_SCK       PIN_SPI_SCK
#define PIN_LORA_MISO      PIN_SPI_MISO
#define PIN_LORA_MOSI      PIN_SPI_MOSI
#define PIN_LORA_CS        3
#define PIN_LORA_RST       4
#define PIN_LORA_DIO1      5
#define PIN_LORA_BUSY      6
#define PIN_LORA_POWER     46   // powers the whole radio module, active high
#define LORA_TCXO_V        2.4f

// TCA8418 keyboard, touch controller, BQ25896 and BQ27220 share Wire.
#define PIN_I2C_SDA        13
#define PIN_I2C_SCL        14
#define I2C_FREQ           400000
#define KB_I2C_ADDR        0x34
#define PIN_KB_INT         15   // active low
#define PIN_KB_BL          42   // keyboard backlight, PWM
#define KB_BL_PWM_CH       1
#define TOUCH_I2C_ADDR     0x1A
#define PIN_TOUCH_INT      12
#define PIN_TOUCH_RST      38
#define BQ25896_ADDR       0x6B
#define BQ27220_ADDR       0x55

// Side button: the screen button.
#define PIN_BOOT_BTN       0

// MIA-M10Q GPS on UART1, powered through an enable pin.
#define PIN_GPS_RX         44
#define PIN_GPS_TX         43
#define PIN_GPS_EN         39   // active high
#define GPS_BAUD           38400

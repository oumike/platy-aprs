#pragma once
// Seeed Wio Tracker L2 — ESP32-S3, 16 MB flash, 8 MB OPI PSRAM, NV3031B 2.4"
// quad-SPI panel, GT911 touch, SX1262, L76K GPS, SD over SDMMC. Source:
// camillia-mt src/hal/hw_wio_tracker_l2.h (transcribed from Seeed's variant).
//
// Almost every rail sits behind a PCA9555 expander, so bring-up order is
// I2C -> expander -> everything else (see wio_l2_io.cpp).

#define BOARD_NAME         "Wio Tracker L2"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       0
#define HAS_BUZZER         0
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         1

// Shared I2C: expander, touch, LP5814 backlight driver, ADS1115, codecs.
#define PIN_I2C_SDA        47
#define PIN_I2C_SCL        48
#define I2C_FREQ           100000

// PCA9555 expander and its bits.
#define EXPANDER_ADDR      0x21
#define PIN_EXPANDER_INT   45
#define EXP_WAKE_BUTTON    0    // in, active low
#define EXP_I2C_INT        1    // in
#define EXP_SD_DETECT      2    // in
#define EXP_TOUCH_INT      3    // driven low; touch is polled
#define EXP_LCD_CS         4
#define EXP_LCD_POWER      5
#define EXP_LCD_RST        6
#define EXP_GROVE_POWER    7
#define EXP_TOUCH_RST      8
#define EXP_GNSS_RST       9    // active HIGH
#define EXP_USER_LED       10
#define EXP_USB_OTG_EN     11
#define EXP_AUDIO_PA       12
#define EXP_GNSS_POWER     13
#define EXP_SD_POWER       14
#define EXP_BATT_SENSE_EN  15

// NV3031B on quad SPI (no DC, reset on the expander).
#define PIN_TFT_SCK        42
#define PIN_TFT_IO0        41
#define PIN_TFT_IO1        40
#define PIN_TFT_IO2        39
#define PIN_TFT_IO3        38
#define PIN_TFT_CS         46
#define TFT_PANEL_W        240
#define TFT_PANEL_H        320
#define TFT_ROTATION       0    // with the panel's offset_rotation 1 -> 320x240

// LP5814 I2C backlight driver.
#define LP5814_ADDR        0x2C

// GT911 touch on the shared bus.
#define TOUCH_ADDR         0x5D
#define TOUCH_OFFSET_ROT   2

// SX1262 on its own SPI pins; TCXO on DIO3, DIO2 drives the RF switch.
#define PIN_LORA_SCK       4
#define PIN_LORA_MISO      5
#define PIN_LORA_MOSI      6
#define PIN_LORA_CS        21
#define PIN_LORA_RST       7
#define PIN_LORA_DIO1      9
#define PIN_LORA_BUSY      8
#define LORA_TCXO_V        1.8f

// L76K GPS on UART1 (power and reset on the expander).
#define PIN_GPS_RX         18
#define PIN_GPS_TX         17
#define GPS_BAUD           9600

// microSD on SDMMC, 1-bit.
#define PIN_SDMMC_CLK      2
#define PIN_SDMMC_CMD      3
#define PIN_SDMMC_D0       1

// Battery via ADS1115 channel 0 (battery / 2), sense gated by the expander.
#define ADS1115_ADDR       0x48
#define BATT_DIVIDER       2.0f

// The only MCU-side button (BOOT).
#define PIN_USER_BUTTON    0

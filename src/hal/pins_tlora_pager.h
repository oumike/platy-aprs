#pragma once
// LilyGo T-LoRa Pager — ESP32-S3, 16 MB flash, 8 MB PSRAM, 2.33" ST7796 222x480
// run as 480x222 landscape, TCA8418 keyboard, rotary wheel, SX1262, L76K GPS,
// ES8311 speaker, BQ25896 charger, microSD. Rails on an XL9555 expander.
// No touch. Source: camillia-mt src/hal/hw_tlora_pager.h.

#define BOARD_NAME         "T-LoRa Pager"
#define HAS_TOUCH          0
#define HAS_KEYBOARD       1
#define HAS_KB_BLINK       1    // keyboard backlight blinks for unread messages, screen off
#define HAS_BUZZER         0    // ES8311 speaker instead (below)
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0

// Shared SPI bus: LCD + SX1262 + microSD.
#define PIN_SPI_SCK        35
#define PIN_SPI_MISO       33
#define PIN_SPI_MOSI       34
#define PIN_TFT_CS         38
#define PIN_TFT_DC         37
#define PIN_TFT_RST        -1
#define PIN_TFT_BL         42
#define PIN_SD_CS          21
#define TFT_PANEL_W        222
#define TFT_PANEL_H        480
#define TFT_OFFSET_X       49   // the controller's RAM is wider than the glass
#define TFT_ROTATION       3    // landscape, keyboard below the screen
#define TFT_BL_PWM_CH      7

#define PIN_LORA_SCK       PIN_SPI_SCK
#define PIN_LORA_MISO      PIN_SPI_MISO
#define PIN_LORA_MOSI      PIN_SPI_MOSI
#define PIN_LORA_CS        36
#define PIN_LORA_RST       47
#define PIN_LORA_DIO1      14
#define PIN_LORA_BUSY      48
#define LORA_TCXO_V        3.0f

// XL9555 expander, TCA8418 keyboard, ES8311 codec and BQ25896 charger share Wire.
#define PIN_I2C_SDA        3
#define PIN_I2C_SCL        2
#define I2C_FREQ           400000
#define KB_I2C_ADDR        0x34
#define PIN_KB_INT         6    // active low
#define PIN_KB_BL          46   // keyboard backlight, active high
#define BQ25896_ADDR       0x6B

// XL9555 output bits (address scanned 0x20-0x27).
#define XL_DRV_EN          0    // haptic driver: unused, off
#define XL_AMP_EN          1    // speaker amp: on only while a sound plays
#define XL_KB_RST          2
#define XL_LORA_EN         3
#define XL_GPS_EN          4
#define XL_NFC_EN          5    // unused, off
#define XL_GPS_RST         7
#define XL_KB_EN           8
#define XL_GPIO_EN         9
#define XL_SD_PULLEN       11   // left as an input
#define XL_SD_EN           12

// Rotary wheel: A/B quadrature plus a push; BOOT is the screen button.
#define PIN_ROT_A          40
#define PIN_ROT_B          41
#define PIN_ROT_CLICK      7
#define PIN_BOOT_BTN       0

// L76K GPS on UART1 (rail on the expander).
#define PIN_GPS_RX         4
#define PIN_GPS_TX         12
#define GPS_BAUD           38400

// ES8311 DAC on I2S; the amp is XL_AMP_EN.
#define HAS_I2S_AUDIO      1
#define HAS_ES8311         1
#define AUDIO_CODEC_ADDR   0x18
#define PIN_I2S_MCLK       10
#define PIN_I2S_BCK        11
#define PIN_I2S_WS         18
#define PIN_I2S_DOUT       45
#define AUDIO_AMP_SETTLE_MS 8
#define AUDIO_TONE_AMPLITUDE 7800.0f

#pragma once
// LilyGo T-Deck Plus — ESP32-S3, 16 MB flash, 8 MB OPI PSRAM, 2.8" ST7789,
// ESP32-C3 I2C keyboard, trackball, GT911 touch, SX1262, L76K GPS, microSD.
// Source: camillia-mt src/hal/hw_tdeck.h.

#define BOARD_NAME         "T-Deck Plus"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       1
#define HAS_BUZZER         0    // I2S speaker instead (below)
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0

// Must be held HIGH or the peripherals (and the keyboard MCU) stay unpowered.
#define PIN_POWER_ON       10

// Shared SPI bus: LCD + SX1262 + microSD.
#define PIN_SPI_SCK        40
#define PIN_SPI_MISO       38
#define PIN_SPI_MOSI       41
#define PIN_TFT_CS         12
#define PIN_TFT_DC         11
#define PIN_TFT_RST        -1
#define PIN_TFT_BL         42
#define PIN_SD_CS          39
#define TFT_PANEL_W        240
#define TFT_PANEL_H        320
#define TFT_ROTATION       1    // landscape, keyboard below the screen
#define TFT_BL_PWM_CH      7

#define PIN_LORA_SCK       PIN_SPI_SCK
#define PIN_LORA_MISO      PIN_SPI_MISO
#define PIN_LORA_MOSI      PIN_SPI_MOSI
#define PIN_LORA_CS        9
#define PIN_LORA_RST       17
#define PIN_LORA_DIO1      45
#define PIN_LORA_BUSY      13
#define LORA_TCXO_V        1.6f

// Keyboard MCU and GT911 touch share Wire.
#define PIN_I2C_SDA        18
#define PIN_I2C_SCL        8
#define I2C_FREQ           400000
#define KB_I2C_ADDR        0x55
#define PIN_KB_INT         46
#define TOUCH_ADDR         0x5D
#define PIN_TOUCH_INT      16

// Trackball: each direction pulses its GPIO low as the ball rolls.
#define PIN_TB_UP          3
#define PIN_TB_DOWN        2
#define PIN_TB_LEFT        1
#define PIN_TB_RIGHT       15
#define PIN_TB_CLICK       0

// L76K GPS on UART1.
#define PIN_GPS_RX         44
#define PIN_GPS_TX         43
#define GPS_BAUD           38400

// I2S class-D amplifier (no codec, no enable pin: powered with PIN_POWER_ON).
#define HAS_I2S_AUDIO      1
#define PIN_I2S_WS         5
#define PIN_I2S_BCK        7
#define PIN_I2S_DOUT       6
#define AUDIO_TONE_AMPLITUDE 2800.0f

#define PIN_BATT_ADC       4
#define BATT_DIVIDER       2.06f   // 2:1 divider, x1.03 calibration

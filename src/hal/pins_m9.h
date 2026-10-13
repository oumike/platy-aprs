#pragma once
// Elecrow ThinkNode M9 pin map. Sources: camillia-mt src/hal/hw_m9.h and
// wadamesh variants/thinknode_m9/M9_PORT.md (hardware-verified there).

#define BOARD_NAME         "ThinkNode M9"
#define HAS_TOUCH          0
#define HAS_KEYBOARD       1
#define HAS_BUZZER         1
#define LORA_CHIP_LR1110   1
#define SD_USE_MMC         0

// Power rails — both active LOW.
#define PIN_PERIPH_POWER   18   // P-MOS: LCD / GPS / sensor rail
#define PIN_TFT_BL         17   // PNP backlight

// Shared SPI bus: LCD + LR1110 + microSD.
#define PIN_SPI_SCK        40
#define PIN_SPI_MISO       38
#define PIN_SPI_MOSI       47
#define PIN_TFT_CS         16
#define PIN_TFT_DC         15
#define PIN_TFT_RST        14
#define PIN_SD_CS          48
#define PIN_LORA_CS        39   // must be held HIGH while the radio is unused
#define PIN_LORA_RST       45
#define PIN_LORA_BUSY      41
#define PIN_LORA_DIO1      42
#define PIN_LORA_SCK       PIN_SPI_SCK
#define PIN_LORA_MISO      PIN_SPI_MISO
#define PIN_LORA_MOSI      PIN_SPI_MOSI
#define LORA_TCXO_V        3.3f

#define TFT_PANEL_W        240
#define TFT_PANEL_H        320
#define TFT_ROTATION       3    // landscape, keyboard below the screen
#define TFT_BL_PWM_CH      7    // keep off ch0, which tone() claims

// Keyboard controller on its own bus (GPIO20/21 are the USB pads; released at init).
#define PIN_KB_SDA         20
#define PIN_KB_SCL         21
#define KB_I2C_ADDR        0x6C
#define KB_REG_HW_VER      0x00
#define KB_REG_KEY_EARLY   0x01   // early ESP32-S2 controller (hw 0x03 / fw 0x10)
#define KB_REG_KEY         0x05   // production STC8H controller
#define KB_REG_FW_VER      0xFE

// Key codes delivered by the controller.
#define M9_KEY_ENTER       0x0D
#define M9_KEY_BKSP        0x08
#define M9_KEY_LEFT        0xB4
#define M9_KEY_UP          0xB5
#define M9_KEY_DOWN        0xB6
#define M9_KEY_RIGHT       0xB7
#define M9_KEY_MESSAGE     0x81
#define M9_KEY_HOME        0x82
#define M9_KEY_SUB_HOME    0x83  // function button below Home
#define M9_KEY_SUB_BACK    0x84  // function button below Back
#define M9_KEY_MAP         0x85
#define M9_KEY_BACK        0x86
#define M9_KEY_GPS_LONG    0x87
#define M9_KEY_CTRL        0x90
#define M9_KEY_ENTER_LONG  0xA3

// Peripheral I2C (RTC PCF8563 0x51, IMU 0x6B, compass 0x7C).
#define PIN_I2C_SDA        7
#define PIN_I2C_SCL        6

// GPS CC1167Q on UART1. RX is the pin we receive on.
#define PIN_GPS_RX         2
#define PIN_GPS_TX         3
#define PIN_GPS_EN         11   // active LOW
#define PIN_GPS_RST        5    // HIGH asserts reset
#define GPS_BAUD           115200

#define PIN_BUZZER         9
#define PIN_BATT_ADC       13   // ADC2, 2:1 divider
#define BATT_DIVIDER       2.0f

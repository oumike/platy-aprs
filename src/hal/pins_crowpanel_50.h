#pragma once
// Elecrow CrowPanel Advance 5.0" HMI — ESP32-S3-WROOM-1-N16R8 (16 MB flash,
// 8 MB OPI PSRAM), 800x480 ST7262 panel on the 16-bit RGB bus, GT911 touch,
// SX1262 module in the J8/J9 wireless slot. No keyboard, GPS, battery sense or
// free button. Sources: camillia-mt src/hal/hw_crowpanel_50.h and wadamesh
// variants/crowpanel_50 (Meshtastic's elecrow_panel variant, Elecrow's code).
//
// The DIP switch on the back must select the wireless module (S1=0, S0=1): the
// radio, SD card and speaker share GPIO 2/4/5/6/19/20 through it, so with the
// radio connected there is no SD card. GPIO19/20 are the radio's, so the
// console is UART0 through the CH340.
//
// Backlight and touch reset belong to a PCA9557 at 0x18 (V1.0) or a board MCU
// at 0x30 (V1.1+); display.cpp works out which at boot. The radio's NSS is
// GPIO8 from V1.2 (what Elecrow sells now); -DCROWPANEL_50_HW_V10=1 builds for
// V1.0, where it is GPIO0.

#define BOARD_NAME         "CrowPanel 5.0"
#define HAS_TOUCH          1
#define HAS_KEYBOARD       0
#define HAS_BUZZER         0
#define HAS_BATTERY        0
#define HAS_SD_CARD        0
#define HAS_ROTATION       1
#define HAS_WAKE_BUTTON    0    // double tap wakes the screen from off as well
#define LORA_CHIP_SX1262   1
#define SD_USE_MMC         0

#define TFT_PANEL_W        800
#define TFT_PANEL_H        480
#define TFT_ROTATION       0
// Pixel clock: 14 MHz leaves PSRAM bandwidth for Wi-Fi and the radio while the
// panel scans out of the same memory; 12 MHz sends the panel into its
// self-test (wadamesh).
#define CROWPANEL_PCLK_HZ  14000000

// GT911 touch and the backlight controller share these pins.
#define PIN_I2C_SDA        15
#define PIN_I2C_SCL        16
#define I2C_FREQ           400000
#define PIN_TOUCH_INT      1    // address strap only; polled
#define TOUCH_ADDR         0x5D
#define CROWPANEL_IOEX_ADDR      0x18   // PCA9557 (V1.0)
#define CROWPANEL_IOEX_BL        1      // backlight enable, HIGH = on
#define CROWPANEL_IOEX_TP_RST    2      // GT911 reset, LOW = in reset
#define CROWPANEL_IOEX_AMP_MUTE  3
#define CROWPANEL_IOEX_AMP_SHUT  4
#define CROWPANEL_MCU_ADDR       0x30   // board MCU (V1.1+): 0 brightest .. 244 dimmest
#define CROWPANEL_MCU_BL_MAX     0
#define CROWPANEL_MCU_BL_MIN     244
#define CROWPANEL_MCU_BL_OFF     245
#define CROWPANEL_MCU_TOUCH_ON   250

// SX1262 in the wireless slot: TCXO on DIO3 at 3.3 V, DIO2 drives the RF switch.
#ifndef CROWPANEL_50_HW_V10
#define CROWPANEL_50_HW_V10 0
#endif
#define PIN_LORA_SCK       5
#define PIN_LORA_MISO      4
#define PIN_LORA_MOSI      6
#if CROWPANEL_50_HW_V10
#define PIN_LORA_CS        0
#else
#define PIN_LORA_CS        8
#endif
#define PIN_LORA_RST       19
#define PIN_LORA_DIO1      20
#define PIN_LORA_BUSY      2
#define LORA_TCXO_V        3.3f

// No GPS: its usual pins are the radio's.
#define PIN_GPS_RX         -1
#define PIN_GPS_TX         -1
#define GPS_BAUD           9600

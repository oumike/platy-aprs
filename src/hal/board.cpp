#include "board.h"
#include "board_pins.h"
#include <Arduino.h>
#if defined(DEVICE_WIO_TRACKER_L2)
#include <Wire.h>
#include "wio_l2_io.h"
#endif

static uint16_t s_battMv = 0;
static uint32_t s_battLastMs = 0;

#if defined(DEVICE_WIO_TRACKER_L2)
// ADS1115 single shot, AIN0 vs GND, +/-2.048 V, 128 SPS.
static bool adsReadMv(uint32_t &mv) {
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x01);
    Wire.write(0xC5);
    Wire.write(0x83);
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADS1115_ADDR, 2) != 2) return false;
    const int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
    if (raw <= 0) return false;
    mv = (uint32_t)raw * 2048UL / 32768UL;
    return true;
}
#endif

void boardBegin() {
#if defined(DEVICE_M9)
    // Every device on the shared SPI bus must be deselected before the panel
    // starts talking, or the LR1110 / SD card can drive MISO.
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    pinMode(PIN_PERIPH_POWER, OUTPUT);
    digitalWrite(PIN_PERIPH_POWER, LOW);   // rail ON
    delay(50);

    // GPS off until gpsBegin() decides (EN active-LOW; RST HIGH asserts reset).
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, HIGH);
    pinMode(PIN_GPS_RST, OUTPUT);
    digitalWrite(PIN_GPS_RST, LOW);

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
#elif defined(DEVICE_TDECK)
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
    delay(100);   // keyboard MCU and touch controller boot
#elif defined(DEVICE_WIO_TRACKER_L2)
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    wioIoBegin();
#endif
}

uint16_t boardBatteryMilliVolts() {
    const uint32_t now = millis();
    if (s_battMv != 0 && now - s_battLastMs < 5000) return s_battMv;
    s_battLastMs = now;
    uint32_t sum = 0;
    int good = 0;
#if defined(DEVICE_WIO_TRACKER_L2)
    if (wioIoSet(EXP_BATT_SENSE_EN, true)) {
        delay(10);
        for (int i = 0; i < 4; i++) {
            uint32_t mv;
            if (adsReadMv(mv) && mv > 500 && mv < 2400) {
                sum += mv;
                good++;
            }
        }
        wioIoSet(EXP_BATT_SENSE_EN, false);
    }
#else
    // On the M9 this is ADC2, which is arbitrated with Wi-Fi; a failed or
    // nonsense sample is dropped and the previous value kept.
    for (int i = 0; i < 8; i++) {
        const uint32_t mv = analogReadMilliVolts(PIN_BATT_ADC);
        if (mv > 500 && mv < 2400) {
            sum += mv;
            good++;
        }
    }
#endif
    if (good > 0) {
        const uint16_t mv = (uint16_t)((sum / good) * BATT_DIVIDER);
        s_battMv = s_battMv == 0 ? mv : (uint16_t)((s_battMv * 3 + mv) / 4);
    }
    return s_battMv;
}

int boardBatteryPercent() {
    const uint16_t mv = boardBatteryMilliVolts();
    if (mv == 0) return -1;
    if (mv >= 4150) return 100;
    if (mv <= 3300) return 0;
    return (int)((mv - 3300) * 100 / (4150 - 3300));
}

bool boardHasBuzzer() { return HAS_BUZZER; }

const char *boardName() { return BOARD_NAME; }

void boardBeep(uint16_t freqHz, uint16_t ms) {
#if HAS_BUZZER
    tone(PIN_BUZZER, freqHz, ms);
#else
    (void)freqHz;
    (void)ms;
#endif
}

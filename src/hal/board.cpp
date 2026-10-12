#include "board.h"
#include "pins_m9.h"
#include <Arduino.h>

static uint16_t s_battMv = 0;
static uint32_t s_battLastMs = 0;

void boardBegin() {
    // Every device on the shared SPI bus must be deselected before the panel
    // starts talking, or the LR1110 / SD card can drive MISO.
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    pinMode(PIN_PERIPH_POWER, OUTPUT);
    digitalWrite(PIN_PERIPH_POWER, LOW);   // rail ON
    delay(50);

    // GPS held off until milestone 3 brings it up.
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, HIGH);
    pinMode(PIN_GPS_RST, OUTPUT);
    digitalWrite(PIN_GPS_RST, LOW);

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
}

uint16_t boardBatteryMilliVolts() {
    const uint32_t now = millis();
    if (s_battMv != 0 && now - s_battLastMs < 5000) return s_battMv;
    s_battLastMs = now;
    // ADC2 is arbitrated with Wi-Fi on the S3; a failed or nonsense sample is
    // dropped and the previous value kept.
    uint32_t sum = 0;
    int good = 0;
    for (int i = 0; i < 8; i++) {
        const uint32_t mv = analogReadMilliVolts(PIN_BATT_ADC);
        if (mv > 500 && mv < 2400) {
            sum += mv;
            good++;
        }
    }
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

void boardBeep(uint16_t freqHz, uint16_t ms) {
    tone(PIN_BUZZER, freqHz, ms);
}

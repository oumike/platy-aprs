#if defined(DEVICE_WIO_TRACKER_L2)
#include "board_pins.h"
#include "wio_l2_io.h"
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Bring-up sequence from camillia-mt's wio_tracker_l2_io.cpp.

namespace {

constexpr uint8_t kRegIn0 = 0x00, kRegOut0 = 0x02, kRegCfg0 = 0x06;

uint8_t s_out[2] = {0xFF, 0xFF};
uint8_t s_cfg[2] = {0xFF, 0xFF};
bool s_ready = false;
// wioIoSet runs from loop() and from the audio task.
SemaphoreHandle_t s_lock = nullptr;

bool writeReg(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool readReg(uint8_t reg, uint8_t &v) {
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)EXPANDER_ADDR, 1) != 1) return false;
    v = Wire.read();
    return true;
}

bool commit() {
    return writeReg(kRegOut0, s_out[0]) && writeReg(kRegOut0 + 1, s_out[1]) &&
           writeReg(kRegCfg0, s_cfg[0]) && writeReg(kRegCfg0 + 1, s_cfg[1]);
}

void stage(uint8_t bit, bool high) {
    const uint8_t m = (uint8_t)(1u << (bit & 7));
    s_cfg[bit >> 3] &= (uint8_t)~m;
    if (high) s_out[bit >> 3] |= m;
    else s_out[bit >> 3] &= (uint8_t)~m;
}

void input(uint8_t bit) { s_cfg[bit >> 3] |= (uint8_t)(1u << (bit & 7)); }

}  // namespace

bool wioIoBegin() {
    s_ready = false;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    pinMode(PIN_EXPANDER_INT, INPUT_PULLUP);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    if (!readReg(kRegOut0, s_out[0]) || !readReg(kRegOut0 + 1, s_out[1]) ||
        !readReg(kRegCfg0, s_cfg[0]) || !readReg(kRegCfg0 + 1, s_cfg[1])) {
        Serial.println("[wio-io] expander not answering");
        return false;
    }
    input(EXP_WAKE_BUTTON);
    input(EXP_I2C_INT);
    input(EXP_SD_DETECT);
    input(EXP_LCD_CS);
    stage(EXP_TOUCH_INT, false);
    stage(EXP_LCD_POWER, true);
    stage(EXP_LCD_RST, true);
    stage(EXP_GROVE_POWER, false);
    stage(EXP_TOUCH_RST, false);
    stage(EXP_GNSS_RST, true);
    stage(EXP_USER_LED, false);
    stage(EXP_USB_OTG_EN, false);
    stage(EXP_AUDIO_PA, false);
    stage(EXP_GNSS_POWER, false);
    stage(EXP_SD_POWER, false);
    stage(EXP_BATT_SENSE_EN, false);
    bool ok = commit();
    delay(10);
    stage(EXP_GNSS_RST, false);
    ok = commit() && ok;
    delay(40);
    stage(EXP_LCD_RST, false);
    ok = commit() && ok;
    delay(10);
    stage(EXP_LCD_RST, true);
    ok = commit() && ok;
    delay(500);
    stage(EXP_LCD_CS, true);
    ok = commit() && ok;
    delay(10);
    stage(EXP_TOUCH_RST, true);
    ok = commit() && ok;
    delay(60);
    s_ready = ok;
    Serial.printf("[wio-io] %s out=%02X%02X cfg=%02X%02X\n", ok ? "ready" : "write failed", s_out[1],
                  s_out[0], s_cfg[1], s_cfg[0]);
    return ok;
}

bool wioIoReady() { return s_ready; }

bool wioIoSet(uint8_t bit, bool high) {
    if (!s_ready) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t out[2] = {s_out[0], s_out[1]}, cfg[2] = {s_cfg[0], s_cfg[1]};
    stage(bit, high);
    const bool ok = commit();
    if (!ok) {
        memcpy(s_out, out, 2);
        memcpy(s_cfg, cfg, 2);
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool wioIoWakePressed() {
    uint8_t in = 0xFF;
    if (!s_ready || !readReg(kRegIn0, in)) return false;
    return (in & (1u << EXP_WAKE_BUTTON)) == 0;
}
#endif

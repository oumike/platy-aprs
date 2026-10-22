#if defined(DEVICE_TDECK_PRO)
#include "tdeck_pro_io.h"
#include "board_pins.h"
#include "keys.h"
#include <Arduino.h>
#include <Wire.h>

namespace {

bool i2cWrite1(uint8_t addr, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *v, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)Wire.read();
    return true;
}

uint8_t i2cRead1(uint8_t addr, uint8_t reg) {
    uint8_t v = 0;
    return i2cRead(addr, reg, &v, 1) ? v : 0;
}

// ── BQ25896 ─────────────────────────────────────────────────────────────────
// Same handling as the T-LoRa Pager: one-shot conversions, watchdog petted.
constexpr uint8_t kBqAdcCtrl = 0x02, kBqWatchdog = 0x03, kBqBatV = 0x0E;
bool s_bqPending = false;
uint16_t s_bqMv = 0;

// ── BQ27220 ─────────────────────────────────────────────────────────────────
constexpr uint8_t kGaugeSoc = 0x2C;
bool s_gaugeFailed = false;

// ── TCA8418 ─────────────────────────────────────────────────────────────────
constexpr uint8_t kTcaIntStat = 0x02, kTcaKeyLckEc = 0x03, kTcaKeyEventA = 0x04;
constexpr uint8_t kKeyAlt = 30, kKeyShiftL = 31, kKeySym = 32, kKeyShiftR = 35, kKeyCount = 35;
constexpr uint32_t kModTimeoutMs = 1500;
constexpr uint8_t kModShift = 1, kModSym = 2, kModAlt = 4;
uint8_t s_mod = 0;
uint32_t s_modMs = 0;
bool s_altDown = false;

// Plain and Sym per key number - 1 (camillia-mt's map; the matrix starts at
// the top right, so 1 is 'p'). Shift capitalises letters. Sym+Backspace is
// Back and Sym+Space is Home, as on the T-LoRa Pager.
const uint8_t kMap[kKeyCount][2] = {
    {'p', '@'}, {'o', '+'}, {'i', '-'}, {'u', '_'}, {'y', ')'},
    {'t', '('}, {'r', '3'}, {'e', '2'}, {'w', '1'}, {'q', '#'},
    {RK_BKSP, RK_BACK}, {'l', '"'}, {'k', '\''}, {'j', ';'}, {'h', ':'},
    {'g', '/'}, {'f', '6'}, {'d', '5'}, {'s', '4'}, {'a', '*'},
    {RK_ENTER, RK_TAB}, {'$', '$'}, {'m', '.'}, {'n', ','}, {'b', '!'},
    {'v', '?'}, {'c', '9'}, {'x', '8'}, {'z', '7'}, {0, 0},
    {0, 0}, {0, 0}, {' ', RK_HOME}, {0, '0'}, {0, 0},
};

// Alt chords by key number: WASD are the arrows (the keyboard has no other
// way to move focus), the rest match the T-Deck Plus hotkeys.
uint8_t altChord(uint8_t keyNum) {
    switch (keyNum) {
        case 9:  return RK_UP;          // W
        case 20: return RK_LEFT;        // A
        case 19: return RK_DOWN;        // S
        case 18: return RK_RIGHT;       // D
        case 17: return RK_FAVORITE;    // F
        case 11: return RK_BACK;        // Backspace
        case 15: return RK_HOME;        // H
        case 23: return RK_MESSAGE;     // M
        case 1:  return RK_MAP;         // P
        case 12: return RK_MONITOR;     // L
        case 6:  return RK_SUB_BACK;    // T: Stations
        case 21: return RK_TAB;         // Enter
        default: return 0;
    }
}

uint8_t translate(uint8_t keyNum, bool pressed, uint32_t now) {
    if (keyNum == kKeyAlt) {
        // Alt works held (every key while it is down) and as a one-shot tap.
        s_altDown = pressed;
        if (pressed) {
            s_mod ^= kModAlt;
            s_modMs = now;
        }
        return 0;
    }
    if (!pressed) return 0;
    if (s_mod && now - s_modMs > kModTimeoutMs) s_mod = 0;
    if (keyNum == kKeyShiftL || keyNum == kKeyShiftR || keyNum == kKeySym) {
        s_mod ^= keyNum == kKeySym ? kModSym : kModShift;
        s_modMs = now;
        return 0;
    }
    if (keyNum < 1 || keyNum > kKeyCount) return 0;
    if (s_altDown || (s_mod & kModAlt)) {
        s_mod = 0;
        return altChord(keyNum);
    }
    const uint8_t *e = kMap[keyNum - 1];
    uint8_t k = (s_mod & kModSym) && e[1] ? e[1] : e[0];
    if ((s_mod & kModShift) && k >= 'a' && k <= 'z') k = (uint8_t)(k - 'a' + 'A');
    s_mod = 0;   // one-shot
    return k;
}

}  // namespace

void tdeckProIoBegin() { Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ); }

uint16_t tdeckProBatteryMv() {
    uint8_t v = 0;
    if (!i2cRead(BQ25896_ADDR, kBqWatchdog, &v, 1)) {
        s_bqPending = false;
        return s_bqMv;
    }
    i2cWrite1(BQ25896_ADDR, kBqWatchdog, (uint8_t)(v | 0x40));
    uint8_t adc = 0;
    if (!i2cRead(BQ25896_ADDR, kBqAdcCtrl, &adc, 1)) return s_bqMv;
    if (s_bqPending && !(adc & 0x80)) {   // CONV_START cleared: result ready
        const uint8_t raw = (uint8_t)(i2cRead1(BQ25896_ADDR, kBqBatV) & 0x7F);
        if (raw) s_bqMv = (uint16_t)(2304 + raw * 20);
        s_bqPending = false;
    }
    if (!s_bqPending) {
        s_bqPending = i2cWrite1(BQ25896_ADDR, kBqAdcCtrl, (uint8_t)((adc & ~0x40) | 0x80));
    }
    return s_bqMv;
}

int tdeckProBatteryPercent() {
    if (s_gaugeFailed) return -1;
    uint8_t v[2];
    if (!i2cRead(BQ27220_ADDR, kGaugeSoc, v, 2)) {
        s_gaugeFailed = true;   // not fitted or not answering: the voltage curve takes over
        Serial.println("[io] BQ27220 not answering; battery from voltage");
        return -1;
    }
    const int pct = v[0] | (v[1] << 8);
    return pct <= 100 ? pct : -1;
}

bool tdeckProKeyboardBegin() {
    Wire.beginTransmission(KB_I2C_ADDR);
    if (Wire.endTransmission() != 0) return false;
    // 4 rows x 10 columns, as on the T-LoRa Pager.
    static const uint8_t kInit[][2] = {
        {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00},   // GPIO_DIR
        {0x20, 0xFF}, {0x21, 0xFF}, {0x22, 0xFF},   // GPI_EM
        {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},   // GPIO_INT_LVL
        {0x1A, 0xFF}, {0x1B, 0xFF}, {0x1C, 0xFF},   // GPIO_INT_EN
        {0x1D, 0x0F}, {0x1E, 0xFF}, {0x1F, 0x03},   // KP_GPIO
        {0x29, 0x00}, {0x2A, 0x00}, {0x2B, 0x00},   // DEBOUNCE_DIS
    };
    for (const auto &r : kInit) i2cWrite1(KB_I2C_ADDR, r[0], r[1]);
    for (int i = 0; i < 16 && i2cRead1(KB_I2C_ADDR, kTcaKeyEventA); i++) {
    }
    i2cWrite1(KB_I2C_ADDR, kTcaIntStat, 0x03);
    s_mod = 0;
    s_altDown = false;
    return true;
}

int tdeckProKeyboardScan(uint8_t *out, int max) {
    int n = 0;
    const uint32_t now = millis();
    for (int sweep = 0; sweep < 4; sweep++) {
        const uint8_t count = (uint8_t)(i2cRead1(KB_I2C_ADDR, kTcaKeyLckEc) & 0x0F);
        i2cWrite1(KB_I2C_ADDR, kTcaIntStat, 0x03);
        if (count == 0) break;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t ev = i2cRead1(KB_I2C_ADDR, kTcaKeyEventA);
            if (ev == 0) break;
            const uint8_t k = translate((uint8_t)(ev & 0x7F), (ev & 0x80) != 0, now);
            if (k && n < max) out[n++] = k;
        }
    }
    return n;
}

#endif

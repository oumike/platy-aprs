#if defined(DEVICE_TLORA_PAGER)
#include "tlora_pager_io.h"
#include "board_pins.h"
#include "keys.h"
#include <Arduino.h>
#include <Wire.h>

namespace {

// ── XL9555 ──────────────────────────────────────────────────────────────────
constexpr uint8_t kXlOut0 = 0x02, kXlCfg0 = 0x06;   // port 1 follows each
int s_xlAddr = -1;
uint8_t s_out[2] = {0xFF, 0xFF}, s_cfg[2] = {0xFF, 0xFF};   // cfg bit 1 = input

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t *v, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v, n);
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

bool i2cWrite1(uint8_t addr, uint8_t reg, uint8_t v) { return i2cWrite(addr, reg, &v, 1); }

uint8_t i2cRead1(uint8_t addr, uint8_t reg) {
    uint8_t v = 0;
    return i2cRead(addr, reg, &v, 1) ? v : 0;
}

void xlOutput(uint8_t bit, bool high) {
    uint8_t &o = s_out[bit / 8];
    const uint8_t m = (uint8_t)(1u << (bit % 8));
    o = high ? (uint8_t)(o | m) : (uint8_t)(o & ~m);
    s_cfg[bit / 8] &= (uint8_t)~m;
}

void xlInput(uint8_t bit) { s_cfg[bit / 8] |= (uint8_t)(1u << (bit % 8)); }

bool xlFlush() {
    return s_xlAddr >= 0 && i2cWrite((uint8_t)s_xlAddr, kXlOut0, s_out, 2) &&
           i2cWrite((uint8_t)s_xlAddr, kXlCfg0, s_cfg, 2);
}

// ── BQ25896 ─────────────────────────────────────────────────────────────────
// One-shot ADC conversions, started on one read and collected on the next, and
// the I2C watchdog petted every time: on expiry the charger restores its
// defaults and the battery reads 0 while the chip still ACKs (camillia-mt).
constexpr uint8_t kBqAdcCtrl = 0x02, kBqWatchdog = 0x03, kBqBatV = 0x0E;
bool s_bqPending = false;
uint16_t s_bqMv = 0;

// ── TCA8418 ─────────────────────────────────────────────────────────────────
constexpr uint8_t kTcaIntStat = 0x02, kTcaKeyLckEc = 0x03, kTcaKeyEventA = 0x04;
constexpr uint8_t kKeyEnter = 20, kKeySym = 21, kKeyShift = 29, kKeyBksp = 30, kKeyCount = 31;
constexpr uint32_t kModTimeoutMs = 1500;
constexpr uint8_t kModShift = 1, kModSym = 2;
uint8_t s_mod = 0;
uint32_t s_modMs = 0;

// Plain and Sym per key number - 1 (camillia-mt's map). Shift capitalises
// letters. Sym+Backspace is Back and Sym+Space is Home: the keyboard has no
// navigation keys of its own.
const uint8_t kMap[kKeyCount][2] = {
    {'q', '1'}, {'w', '2'}, {'e', '3'}, {'r', '4'}, {'t', '5'},
    {'y', '6'}, {'u', '7'}, {'i', '8'}, {'o', '9'}, {'p', '0'},
    {'a', '*'}, {'s', '/'}, {'d', '+'}, {'f', '-'}, {'g', '='},
    {'h', ':'}, {'j', '\''}, {'k', '"'}, {'l', '@'},
    {RK_ENTER, RK_TAB}, {0, 0},
    {'z', '_'}, {'x', '$'}, {'c', ';'}, {'v', '?'}, {'b', '!'}, {'n', ','}, {'m', '.'},
    {0, 0}, {RK_BKSP, RK_BACK}, {' ', RK_HOME},
};

uint8_t translate(uint8_t keyNum, uint32_t now) {
    if (s_mod && now - s_modMs > kModTimeoutMs) s_mod = 0;
    if (keyNum == kKeySym || keyNum == kKeyShift) {
        s_mod ^= keyNum == kKeySym ? kModSym : kModShift;
        s_modMs = now;
        return 0;
    }
    if (keyNum < 1 || keyNum > kKeyCount) return 0;
    const uint8_t *e = kMap[keyNum - 1];
    uint8_t k = (s_mod & kModSym) && e[1] ? e[1] : e[0];
    if ((s_mod & kModShift) && k >= 'a' && k <= 'z') k = (uint8_t)(k - 'a' + 'A');
    s_mod = 0;   // one-shot
    return k;
}

}  // namespace

bool pagerIoBegin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    for (uint8_t a = 0x20; a <= 0x27 && s_xlAddr < 0; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) s_xlAddr = a;
    }
    if (s_xlAddr < 0) {
        Serial.println("[io] XL9555 not found (0x20-0x27)");
        return false;
    }
    (void)i2cRead((uint8_t)s_xlAddr, kXlOut0, s_out, 2);
    (void)i2cRead((uint8_t)s_xlAddr, kXlCfg0, s_cfg, 2);
    xlOutput(XL_DRV_EN, false);
    xlOutput(XL_AMP_EN, false);
    xlOutput(XL_NFC_EN, false);
    for (uint8_t b : {XL_KB_RST, XL_LORA_EN, XL_GPS_EN, XL_GPS_RST, XL_KB_EN, XL_GPIO_EN, XL_SD_EN})
        xlOutput(b, true);
    xlInput(XL_SD_PULLEN);
    const bool ok = xlFlush();
    Serial.printf("[io] XL9555 at 0x%02X %s\n", s_xlAddr, ok ? "armed" : "write failed");
    delay(12);   // rails settle before the radio and keyboard are probed
    return ok;
}

bool pagerIoSet(uint8_t bit, bool high) {
    xlOutput(bit, high);
    return xlFlush();
}

uint16_t pagerBatteryMv() {
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

bool pagerKeyboardBegin() {
    Wire.beginTransmission(KB_I2C_ADDR);
    if (Wire.endTransmission() != 0) return false;
    // 4 rows x 10 columns, all GPIO-event interrupts on (Meshtastic's setup).
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
    return true;
}

int pagerKeyboardScan(uint8_t *out, int max) {
    int n = 0;
    const uint32_t now = millis();
    // Bounded: a controller answering with a stuck count can't spin here.
    for (int sweep = 0; sweep < 4; sweep++) {
        const uint8_t count = (uint8_t)(i2cRead1(KB_I2C_ADDR, kTcaKeyLckEc) & 0x0F);
        // Acknowledged before popping, so a key struck mid-drain re-asserts INT.
        i2cWrite1(KB_I2C_ADDR, kTcaIntStat, 0x03);
        if (count == 0) break;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t ev = i2cRead1(KB_I2C_ADDR, kTcaKeyEventA);   // FIFO head, pops
            if (ev == 0) break;
            if (!(ev & 0x80)) continue;   // releases never reach the modifier state
            const uint8_t k = translate((uint8_t)(ev & 0x7F), now);
            if (k && n < max) out[n++] = k;
        }
    }
    return n;
}

#endif

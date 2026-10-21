#include "board_pins.h"
#if defined(DEVICE_MESH_DECK)
#include "mesh_deck_io.h"
#include "keys.h"
#include <Arduino.h>
#include <Wire.h>

// Register map and bring-up sequence follow camillia-mt (aw9523.cpp,
// main_lvgl.cpp meshDeckI2cBusRecover / meshDeckReleaseTouchReset).
namespace {

constexpr uint8_t kAwInput0 = 0x00;
constexpr uint8_t kAwOutput0 = 0x02;
constexpr uint8_t kAwOutput1 = 0x03;
constexpr uint8_t kAwConfig0 = 0x04;
constexpr uint8_t kAwConfig1 = 0x05;
constexpr uint8_t kAwIntMask0 = 0x06;
constexpr uint8_t kAwIntMask1 = 0x07;
constexpr uint8_t kAwChipId = 0x10;
constexpr uint8_t kAwLedMode0 = 0x12;
constexpr uint8_t kAwLedMode1 = 0x13;
constexpr uint8_t kAwSoftRst = 0x7F;
constexpr uint8_t kTouchRstBit = 1u << 3;   // P13

bool s_expander = false;

bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = (uint8_t)Wire.read();
    return true;
}

// A reset can land mid-transfer and leave a slave holding SDA low; nine clocks
// and a STOP free it. Harmless on an idle bus.
void busRecover() {
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_I2C_SCL, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_I2C_SCL, HIGH);
        delayMicroseconds(5);
    }
    pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SDA, HIGH);
    delayMicroseconds(5);
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
}

// An AW9523 keeps its state across a CPU reset, so soft-reset it once and put
// every pin in GPIO input mode. Never repeat this on the 0x59 expander: it
// would put the touch controller back into reset.
bool awBegin(uint8_t addr) {
    uint8_t id = 0;
    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) {
        if (i) delay(5);
        ok = readRegs(addr, kAwChipId, &id, 1) && id == 0x23;
    }
    if (!ok) return false;
    writeReg(addr, kAwSoftRst, 0x00);
    delay(2);
    return writeReg(addr, kAwLedMode0, 0xFF) && writeReg(addr, kAwLedMode1, 0xFF) &&
           writeReg(addr, kAwConfig0, 0xFF) && writeReg(addr, kAwConfig1, 0xFF) &&
           writeReg(addr, kAwIntMask0, 0xFF) && writeReg(addr, kAwIntMask1, 0xFF);
}

// FT6636 reset is P13, low at power-up; release it before anything probes touch.
void releaseTouchReset() {
    uint8_t cfg = 0xFF, out = 0xFF;
    readRegs(MD_EXPANDER_ADDR, kAwConfig1, &cfg, 1);
    readRegs(MD_EXPANDER_ADDR, kAwOutput1, &out, 1);
    writeReg(MD_EXPANDER_ADDR, kAwOutput1, (uint8_t)(out & ~kTouchRstBit));
    writeReg(MD_EXPANDER_ADDR, kAwConfig1, (uint8_t)(cfg & ~kTouchRstBit));
    delay(10);
    writeReg(MD_EXPANDER_ADDR, kAwOutput1, (uint8_t)(out | kTouchRstBit));
    delay(300);   // controller boot
    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    Serial.printf("[meshdeck] FT6636 %s\n", Wire.endTransmission() == 0 ? "ready" : "not answering");
}

bool gaugeReg16(uint8_t reg, uint16_t &v) {
    uint8_t b[2];
    if (!readRegs(FUEL_GAUGE_ADDR, reg, b, 2)) return false;
    v = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

}  // namespace

bool meshDeckIoBegin() {
    busRecover();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    s_expander = awBegin(MD_EXPANDER_ADDR);
    if (!s_expander) {
        Serial.printf("[meshdeck] expander 0x%02X not found\n", MD_EXPANDER_ADDR);
        return false;
    }
    releaseTouchReset();
    return true;
}

bool meshDeckReadButtons(uint8_t &port0) {
    if (!s_expander) return false;
    uint8_t v;
    // All-zero means every line low at once, which is a bad read, not a chord.
    if (!readRegs(MD_EXPANDER_ADDR, kAwInput0, &v, 1) || v == 0x00) return false;
    port0 = v;
    return true;
}

bool meshDeckReadTouch(int16_t &x, int16_t &y) {
    uint8_t b[7];
    if (!readRegs(TOUCH_ADDR, 0x02, b, 7)) return false;   // TD_STATUS + first point
    const uint8_t points = b[0] & 0x0F;
    if (points == 0 || points > 2) return false;
    x = (int16_t)min((int)(((b[1] & 0x0F) << 8) | b[2]), TFT_PANEL_W - 1);
    y = (int16_t)min((int)(((b[3] & 0x0F) << 8) | b[4]), TFT_PANEL_H - 1);
    return true;
}

uint16_t meshDeckBatteryMv() {
    uint16_t raw;
    if (!gaugeReg16(0x02, raw)) return 0;   // VCELL, 78.125 uV/LSB
    return (uint16_t)((uint32_t)raw * 78125UL / 1000000UL);
}

int meshDeckBatteryPercent() {
    uint16_t raw;
    if (!gaugeReg16(0x04, raw)) return -1;   // SOC, high byte = whole percent
    return min((int)(raw >> 8), 100);
}

// ── Keyboard module ─────────────────────────────────────────────────────────
// Two AW9523s, one per half, each a 5x5 matrix: rows P10..P14 are driven low
// one at a time and columns P00..P04 read back. Matrix positions, debounce and
// scan follow camillia-mt's keyboard.cpp (keymap from the wadamesh MeshCore
// port's attaky_mesh_series variant).
namespace {

constexpr uint8_t kKbAddr[2] = {MD_KB_LEFT_ADDR, MD_KB_RIGHT_ADDR};
constexpr uint8_t kP1Rows = 0x1F, kP1LedOff = 0xE0;   // P15..P17: the half's RGB LED, active low
constexpr uint8_t kReleaseScans = 2;

// Modifiers are matrix keys. Codes below 0x08 never reach the UI.
constexpr char kNone = 0, kShift = 1, kAlt = 2, kSym = 3;

const char kKeymap[2][5][5] = {
    {   // left half
        {'1', '2', '3', '4', '5'},
        {'q', 'w', 'e', 'r', 't'},
        {'a', 's', 'd', 'f', 'g'},
        {kShift, 'z', 'x', 'c', 'v'},
        {kNone, (char)RK_TAB, kAlt, ',', ' '},
    },
    {   // right half
        {'6', '7', '8', '9', '0'},
        {'y', 'u', 'i', 'o', 'p'},
        {'h', 'j', 'k', 'l', (char)RK_BKSP},
        {'b', 'n', 'm', kNone, (char)RK_ENTER},
        {kNone, '.', kNone, kSym, kNone},
    },
};

bool s_kbReady[2] = {};
uint8_t s_kbStable[2][5] = {};    // debounced pressed bits per row
uint8_t s_kbPrev[2][5] = {};      // bits already reported
uint8_t s_kbRelCand[2][5] = {};   // bits counting down to released
uint8_t s_kbRelCount[2][5] = {};

bool kbConfigure(uint8_t half) {
    const uint8_t a = kKbAddr[half];
    return writeReg(a, kAwConfig0, 0xFF) && writeReg(a, kAwConfig1, 0x00) &&   // columns in, rows out
           writeReg(a, kAwOutput0, 0x00) && writeReg(a, kAwOutput1, kP1Rows | kP1LedOff);
}

// Pressed-bit mask per row. The idle rows are driven high, as the working
// wadamesh port does; whether the columns have pull-ups is not documented.
bool kbScanHalf(uint8_t half, uint8_t rows[5]) {
    const uint8_t a = kKbAddr[half];
    for (uint8_t r = 0; r < 5; r++) {
        if (!writeReg(a, kAwOutput1, (uint8_t)((kP1Rows & ~(1u << r)) | kP1LedOff))) return false;
        delayMicroseconds(50);
        uint8_t cols;
        if (!readRegs(a, kAwInput0, &cols, 1)) return false;
        rows[r] = (uint8_t)(~cols & 0x1F);
        // A whole row down is a glitched read (0x00), not five fingers.
        if (rows[r] == 0x1F) return false;
    }
    writeReg(a, kAwOutput1, kP1Rows | kP1LedOff);
    return true;
}

// A press counts on the first scan that sees it; only the release has to hold
// for kReleaseScans reads, which is what absorbs contact bounce.
void kbDebounce(uint8_t half, const uint8_t fresh[5]) {
    for (uint8_t r = 0; r < 5; r++) {
        uint8_t &stable = s_kbStable[half][r];
        stable |= fresh[r];
        const uint8_t rel = (uint8_t)(stable & ~fresh[r]);
        if (!rel) {
            s_kbRelCand[half][r] = s_kbRelCount[half][r] = 0;
        } else if (rel != s_kbRelCand[half][r]) {
            s_kbRelCand[half][r] = rel;
            s_kbRelCount[half][r] = 1;
        } else if (++s_kbRelCount[half][r] >= kReleaseScans) {
            stable &= (uint8_t)~rel;
            s_kbRelCand[half][r] = s_kbRelCount[half][r] = 0;
        }
    }
}

bool kbHeld(char code) {
    for (uint8_t h = 0; h < 2; h++)
        for (uint8_t r = 0; r < 5; r++)
            for (uint8_t c = 0; c < 5; c++)
                if (kKeymap[h][r][c] == code && (s_kbStable[h][r] & (1u << c))) return true;
    return false;
}

// US legends for the number row; letters uppercase.
char kbShifted(char k) {
    if (k >= 'a' && k <= 'z') return (char)(k - 'a' + 'A');
    static const char kNum[] = ")!@#$%^&*(";
    if (k >= '0' && k <= '9') return kNum[k - '0'];
    if (k == ',') return '<';
    if (k == '.') return '>';
    return k;
}

// Symbol layer (Sym held): the punctuation the matrix has no key for.
char kbSymbol(char k) {
    static const char kFrom[] = "qwertyuiopasdfghjklzxcvbnm,.";
    static const char kTo[]   = "?/-_=+[]{}:;'\"`~\\|&<>*@#%^!?";
    for (int i = 0; kFrom[i]; i++)
        if (kFrom[i] == k) return kTo[i];
    return k;
}

// Alt chords: the navigation keys the M9 has as buttons.
uint8_t kbAltNav(char k) {
    switch (k) {
        case 'h': return RK_HOME;
        case 'm': return RK_MESSAGE;
        case 'p': return RK_MAP;
        case 's': return RK_SUB_BACK;   // stations
        case 'l': return RK_MONITOR;
        case (char)RK_BKSP: return RK_BACK;
        default: return 0;
    }
}

}  // namespace

bool meshDeckKeyboardBegin() {
    for (uint8_t h = 0; h < 2; h++) {
        if (!s_kbReady[h]) s_kbReady[h] = awBegin(kKbAddr[h]) && kbConfigure(h);
    }
    return s_kbReady[0] || s_kbReady[1];
}

bool meshDeckKeyboardPresent() { return s_kbReady[0] || s_kbReady[1]; }

int meshDeckKeyboardScan(uint8_t *out, int max) {
    uint8_t fresh[5];
    for (uint8_t h = 0; h < 2; h++) {
        if (s_kbReady[h] && kbScanHalf(h, fresh)) kbDebounce(h, fresh);
    }
    // Modifiers are resolved against this same scan, so a fast Shift+letter
    // still comes out capitalised.
    const bool shift = kbHeld(kShift), alt = kbHeld(kAlt), sym = kbHeld(kSym);
    int n = 0;
    for (uint8_t h = 0; h < 2; h++) {
        for (uint8_t r = 0; r < 5; r++) {
            s_kbPrev[h][r] &= s_kbStable[h][r];
            const uint8_t edges = (uint8_t)(s_kbStable[h][r] & ~s_kbPrev[h][r]);
            s_kbPrev[h][r] |= edges;
            for (uint8_t c = 0; c < 5 && edges; c++) {
                if (!(edges & (1u << c))) continue;
                const char k = kKeymap[h][r][c];
                if (k == kNone || k == kShift || k == kAlt || k == kSym || n >= max) continue;
                uint8_t code = (uint8_t)k;
                if (alt) code = kbAltNav(k);
                else if (sym) code = (uint8_t)kbSymbol(k);
                else if (shift) code = (uint8_t)kbShifted(k);
                if (code) out[n++] = code;
            }
        }
    }
    return n;
}

#endif

#include "keyboard.h"
#include "board_pins.h"
#include "keys.h"
#include "screen.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#if defined(DEVICE_M9)
#include <soc/usb_serial_jtag_reg.h>
#elif defined(DEVICE_WIO_TRACKER_L2)
#include "wio_l2_io.h"
#elif defined(DEVICE_MESH_DECK)
#include "mesh_deck_io.h"
#endif

namespace {

#if HAS_KEYBOARD
bool s_present = false;
#endif
uint32_t s_nextProbeMs = 0;
uint32_t s_nextPollMs = 0;

uint8_t s_ring[32];
uint8_t s_head = 0, s_tail = 0;

KeyboardHotkeyHandler s_hotkey = nullptr;
lv_indev_t *s_indev = nullptr;

// One pending key to report as PRESSED then RELEASED across two read_cb calls.
uint32_t s_pendingKey = 0;
bool s_pressedReported = false;

#if defined(DEVICE_M9)
uint8_t s_keyReg = KB_REG_KEY;

bool readReg(uint8_t reg, uint8_t &value) {
    Wire1.beginTransmission((uint8_t)KB_I2C_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission() != 0) return false;
    if (Wire1.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1) != 1) return false;
    value = Wire1.read();
    return true;
}

void probe() {
    uint8_t hw = 0xFF, fw = 0xFF;
    const bool hwOk = readReg(KB_REG_HW_VER, hw);
    const bool fwOk = readReg(KB_REG_FW_VER, fw);
    if (!hwOk) return;
    s_present = true;
    s_keyReg = (fwOk && hw == 0x03 && fw == 0x10) ? KB_REG_KEY_EARLY : KB_REG_KEY;
    Serial.printf("[kb] controller hw=0x%02X fw=0x%02X keyreg=0x%02X\n", hw, fw, s_keyReg);
}
#elif defined(DEVICE_TDECK)
void probe() {
    Wire.beginTransmission((uint8_t)KB_I2C_ADDR);
    s_present = Wire.endTransmission() == 0;
    if (s_present) Serial.println("[kb] T-Deck keyboard found");
}
#endif

void push(uint8_t k) {
    const uint8_t nh = (uint8_t)((s_head + 1) & 31);
    if (nh == s_tail) return;
    s_ring[s_head] = k;
    s_head = nh;
}

// Board input on its way to the UI. Nothing gets through while the screen is
// dark: each board's wake gesture is handled before this, and every other
// press is dropped rather than acted on unseen.
void input(uint8_t k) {
    if (screenAsleep()) return;
    screenNoteActivity();
    push(k);
}

bool pop(uint8_t &k) {
    if (s_tail == s_head) return false;
    k = s_ring[s_tail];
    s_tail = (uint8_t)((s_tail + 1) & 31);
    return true;
}

bool focusedWantsArrows() {
    lv_group_t *g = lv_group_get_default();
    if (!g) return false;
    if (lv_group_get_editing(g)) return true;
    lv_obj_t *f = lv_group_get_focused(g);
    if (!f) return false;
    if (lv_obj_has_flag(f, LV_OBJ_FLAG_USER_1)) return true;   // e.g. the map
    // One-line text fields keep up/down for moving between fields.
    if (lv_obj_check_type(f, &lv_textarea_class)) return !lv_textarea_get_one_line(f);
    return lv_obj_check_type(f, &lv_dropdown_class) ||
           lv_obj_check_type(f, &lv_roller_class) ||
           lv_obj_check_type(f, &lv_slider_class);
}

bool focusedIsTextarea() {
    lv_group_t *g = lv_group_get_default();
    lv_obj_t *f = g ? lv_group_get_focused(g) : nullptr;
    return f && lv_obj_check_type(f, &lv_textarea_class);
}

bool hiddenInTree(lv_obj_t *o) {
    for (; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return true;
    }
    return false;
}

// Moves focus to the nearest control on screen in the arrow's direction.
// Returns false when there is none (caller falls back to group order).
bool spatialMove(uint8_t dir) {
    lv_group_t *g = lv_group_get_default();
    lv_obj_t *f = g ? lv_group_get_focused(g) : nullptr;
    if (!f) return false;
    lv_obj_update_layout(f);
    lv_area_t fa;
    lv_obj_get_coords(f, &fa);
    const int32_t fcx = (fa.x1 + fa.x2) / 2, fcy = (fa.y1 + fa.y2) / 2;
    const bool vert = dir == RK_UP || dir == RK_DOWN;
    lv_obj_t *best = nullptr;
    int64_t bestScore = INT64_MAX;
    const uint32_t n = lv_group_get_obj_count(g);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *o = lv_group_get_obj_by_index(g, i);
        if (o == f || hiddenInTree(o) || lv_obj_has_state(o, LV_STATE_DISABLED)) continue;
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
        int32_t gap, off;
        if (vert) {
            gap = dir == RK_DOWN ? a.y1 - fa.y2 : fa.y1 - a.y2;
            if (gap < -2) continue;   // not below/above
            off = abs(cx - fcx);
        } else {
            if (a.y1 >= fa.y2 || a.y2 <= fa.y1) continue;   // left/right stay in the same row
            gap = dir == RK_RIGHT ? a.x1 - fa.x2 : fa.x1 - a.x2;
            if (gap < -2) continue;
            off = abs(cy - fcy);
        }
        // Nearest row/column first, then the closest one across.
        const int64_t score = (int64_t)max<int32_t>(gap, 0) * 8 + off;
        if (score < bestScore) {
            bestScore = score;
            best = o;
        }
    }
    if (!best) return false;
    lv_group_focus_obj(best);
    return true;
}

uint32_t mapKey(uint8_t k) {
    switch (k) {
        case RK_UP:
        case RK_DOWN:
            if (focusedWantsArrows()) return k == RK_UP ? LV_KEY_UP : LV_KEY_DOWN;
            if (spatialMove(k)) return 0;
            return k == RK_UP ? LV_KEY_PREV : LV_KEY_NEXT;   // wrap around at the ends
        case RK_LEFT:
        case RK_RIGHT:
            // Text fields move their cursor; everything else moves between controls in a row.
            if (!focusedWantsArrows() && !focusedIsTextarea() && spatialMove(k)) return 0;
            return k == RK_LEFT ? LV_KEY_LEFT : LV_KEY_RIGHT;
        case RK_ENTER: return LV_KEY_ENTER;
        case RK_BKSP:  return LV_KEY_BACKSPACE;
        case RK_TAB:   return LV_KEY_NEXT;
        case RK_BACK:  return LV_KEY_ESC;
        default:
            if (k >= 0x20 && k < 0x7F) return k;
            return 0;
    }
}

void readCb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    if (s_pendingKey && !s_pressedReported) {
        data->key = s_pendingKey;
        data->state = LV_INDEV_STATE_PRESSED;
        s_pressedReported = true;
        data->continue_reading = true;
        return;
    }
    if (s_pendingKey) {
        data->key = s_pendingKey;
        data->state = LV_INDEV_STATE_RELEASED;
        s_pendingKey = 0;
        s_pressedReported = false;
        data->continue_reading = s_tail != s_head;
        return;
    }
    uint8_t raw;
    while (pop(raw)) {
#if defined(DEVICE_TDECK)
        // No Back key: Backspace outside a text field navigates back.
        if (raw == RK_BKSP && !focusedIsTextarea()) raw = RK_BACK;
#endif
        if (s_hotkey && s_hotkey(raw)) continue;
        const uint32_t key = mapKey(raw);
        if (!key) continue;
        s_pendingKey = key;
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
        s_pressedReported = true;
        data->continue_reading = true;
        return;
    }
    data->state = LV_INDEV_STATE_RELEASED;
}


#if defined(DEVICE_TDECK)
// Trackball: each roll step pulses a pin low. Counted in ISRs, turned into
// arrow keys at a limited rate so one flick doesn't skip several items.
volatile int16_t s_tbX = 0, s_tbY = 0;
void IRAM_ATTR tbUp() { s_tbY--; }
void IRAM_ATTR tbDown() { s_tbY++; }
void IRAM_ATTR tbLeft() { s_tbX--; }
void IRAM_ATTR tbRight() { s_tbX++; }
uint32_t s_tbNextMs = 0;
bool s_tbClickDown = false, s_tbClickClaimed = false;
uint32_t s_tbLastMoveMs = 0, s_tbClickSinceMs = 0;
constexpr uint32_t kTbSleepHoldMs = 2000;
#endif

#if defined(DEVICE_M9) || defined(DEVICE_MESH_DECK)
WakeButton s_bootBtn;   // BOOT: tap = screen off, hold = wake
#endif

#if defined(DEVICE_WIO_TRACKER_L2)
bool s_btnDown = false, s_btnLong = false;
uint32_t s_btnSinceMs = 0;
WakeButton s_wakeBtn;   // top Wake button: tap = screen off, hold = wake
#endif

#if defined(DEVICE_MESH_DECK)
// D-pad on expander port 0 (active low). Arrows auto-repeat while held; Select
// is Enter on release, or Back when held (there are no navigation keys).
constexpr uint32_t kMdPollMs = 40, kMdRepeatDelayMs = 450, kMdRepeatMs = 130, kMdHoldBackMs = 700;
uint8_t s_mdPrev = 0xFF;
uint32_t s_mdNextPollMs = 0, s_mdRepeatAtMs = 0, s_mdSelSinceMs = 0;
bool s_mdSelLong = false;
WakeButton s_mdScreenBtn;   // R shoulder button: tap = screen off, hold = wake
#endif

void hwBegin() {
#if defined(DEVICE_M9)
    // GPIO20 is the S3's USB D+ pad; the USB-Serial-JTAG PHY holds it until released.
    REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    delay(5);
    Wire1.begin(PIN_KB_SDA, PIN_KB_SCL, 100000UL);
    delay(50);
    probe();
    if (!s_present) Serial.println("[kb] controller not answering yet; will retry");
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
#elif defined(DEVICE_TDECK)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    probe();
    pinMode(PIN_TB_UP, INPUT_PULLUP);
    pinMode(PIN_TB_DOWN, INPUT_PULLUP);
    pinMode(PIN_TB_LEFT, INPUT_PULLUP);
    pinMode(PIN_TB_RIGHT, INPUT_PULLUP);
    pinMode(PIN_TB_CLICK, INPUT_PULLUP);
    // Pin-to-direction mapping as verified in camillia-mt.
    attachInterrupt(digitalPinToInterrupt(PIN_TB_DOWN), tbRight, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_LEFT), tbLeft, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_RIGHT), tbDown, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_UP), tbUp, FALLING);
#elif defined(DEVICE_WIO_TRACKER_L2)
    pinMode(PIN_USER_BUTTON, INPUT_PULLUP);
#elif defined(DEVICE_MESH_DECK)
    // The d-pad expander was brought up in boardBegin(); the keyboard module
    // shares its bus. One matrix walk is ten transfers, so run it at 400 kHz.
    Wire.setClock(400000UL);
    s_present = meshDeckKeyboardBegin();
    Serial.printf("[kb] Mesh Deck keyboard %s\n", s_present ? "found" : "not found; will retry");
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
#endif
}

void hwPoll(uint32_t now) {
#if defined(DEVICE_M9)
    screenServiceWakeButton(s_bootBtn, digitalRead(PIN_BOOT_BTN) == LOW, now, "BOOT button");
    if (!s_present) {
        if ((int32_t)(now - s_nextProbeMs) < 0) return;
        s_nextProbeMs = now + 1000;
        probe();
        if (!s_present) return;
    }
    // The controller latches a single key; drain a few in case of bursts.
    for (int i = 0; i < 3; i++) {
        uint8_t k = 0;
        if (!readReg(s_keyReg, k)) return;
        if (k == 0x00 || k == 0xFF) return;
#ifdef PLATY_SERIAL_KEYS
        Serial.printf("[kb] key 0x%02X\n", k);
#endif
        // Centre held: the controller times it and sends its own code.
        if (k == M9_KEY_ENTER_LONG) {
            if (screenAsleep()) screenTryWake("centre hold");
            else screenSleep("centre hold");
            continue;
        }
        // The keys face outward with no lid, so only the d-pad centre wakes
        // the screen; any other key pressed while it is dark is dropped.
        if (screenAsleep()) {
            if (k == M9_KEY_ENTER) screenTryWake("centre");
            continue;
        }
        input(k);
    }
#elif defined(DEVICE_TDECK)
    if ((int32_t)(now - s_tbNextMs) >= 0) {
        noInterrupts();
        const int16_t dx = s_tbX, dy = s_tbY;
        s_tbX = s_tbY = 0;
        interrupts();
        if (dx || dy) {
            s_tbNextMs = now + 45;
            s_tbLastMoveMs = now;
            if (abs(dx) > abs(dy)) input(dx > 0 ? RK_RIGHT : RK_LEFT);
            else input(dy > 0 ? RK_DOWN : RK_UP);
        }
    }
    // The click is the only wake gesture (the keyboard and touch panel are
    // exposed to pockets), and holding it 2 s puts the screen to sleep, so
    // Enter goes out on release. The switch chatters while rolling; clicks
    // right after motion are ignored.
    const bool click = digitalRead(PIN_TB_CLICK) == LOW;
    if (click && !s_tbClickDown) {
        s_tbClickSinceMs = now;
        s_tbClickClaimed = now - s_tbLastMoveMs <= 160;
    }
    if (click && !s_tbClickClaimed) {
        if (screenAsleep()) s_tbClickClaimed = screenTryWake("trackball click");
        else if (now - s_tbClickSinceMs >= kTbSleepHoldMs) {
            s_tbClickClaimed = true;
            screenSleep("trackball hold");
        }
    } else if (!click && s_tbClickDown && !s_tbClickClaimed) {
        input(RK_ENTER);
    }
    s_tbClickDown = click;

    if (!s_present) {
        if ((int32_t)(now - s_nextProbeMs) < 0) return;
        s_nextProbeMs = now + 1000;
        probe();
        if (!s_present) return;
    }
    if (Wire.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1) != 1) return;
    const uint8_t k = Wire.read();
    if (k == 0x00) return;
#ifdef PLATY_SERIAL_KEYS
    Serial.printf("[kb] key 0x%02X\n", k);
#endif
    input(k);
#elif defined(DEVICE_WIO_TRACKER_L2)
    // BOOT button: short press = Back, hold = Home. On a dark screen a press
    // wakes it instead (and is used up by that).
    const bool down = digitalRead(PIN_USER_BUTTON) == LOW;
    if (down && !s_btnDown) {
        s_btnSinceMs = now;
        s_btnLong = false;
    }
    if (down && screenAsleep()) {
        screenTryWake("BOOT button");
        s_btnLong = true;
    } else if (down && !s_btnLong && now - s_btnSinceMs > 700) {
        s_btnLong = true;
        input(RK_HOME);
    } else if (!down && s_btnDown && !s_btnLong && now - s_btnSinceMs > 30) {
        input(RK_BACK);
    }
    s_btnDown = down;
    // Top Wake button (on the expander): the screen button. Only read while the
    // expander signals an input change or a press is being timed.
    if (digitalRead(PIN_EXPANDER_INT) == LOW || s_wakeBtn.raw || s_wakeBtn.stable)
        screenServiceWakeButton(s_wakeBtn, wioIoWakePressed(), now, "Wake button");
#elif defined(DEVICE_MESH_DECK)
    if (!s_present && (int32_t)(now - s_nextProbeMs) >= 0) {
        s_nextProbeMs = now + 2000;
        s_present = meshDeckKeyboardBegin();
        if (s_present) Serial.println("[kb] Mesh Deck keyboard found");
    }
    if (s_present) {
        uint8_t keys[8];
        const int n = meshDeckKeyboardScan(keys, (int)sizeof(keys));
        for (int i = 0; i < n; i++) {
#ifdef PLATY_SERIAL_KEYS
            Serial.printf("[kb] key 0x%02X\n", keys[i]);
#endif
            input(keys[i]);
        }
    }
    screenServiceWakeButton(s_bootBtn, digitalRead(PIN_BOOT_BTN) == LOW, now, "BOOT button");
    if ((int32_t)(now - s_mdNextPollMs) < 0) return;
    s_mdNextPollMs = now + kMdPollMs;
    uint8_t port;
    if (!meshDeckReadButtons(port)) return;
    screenServiceWakeButton(s_mdScreenBtn, !(port & (1u << MD_BTN_R2)), now, "R button");
    const uint8_t down = (uint8_t)~port, pressed = (uint8_t)(down & s_mdPrev);
    s_mdPrev = port;
    static const struct { uint8_t bit, key; } kArrows[] = {
        {MD_BTN_UP, RK_UP}, {MD_BTN_DOWN, RK_DOWN}, {MD_BTN_LEFT, RK_LEFT}, {MD_BTN_RIGHT, RK_RIGHT}};
    bool held = false;
    for (const auto &a : kArrows) {
        const uint8_t m = (uint8_t)(1u << a.bit);
        if (pressed & m) {
            input(a.key);
            s_mdRepeatAtMs = now + kMdRepeatDelayMs;
        } else if ((down & m) && !held && (int32_t)(now - s_mdRepeatAtMs) >= 0) {
            input(a.key);
            s_mdRepeatAtMs = now + kMdRepeatMs;
        }
        if (down & m) held = true;
    }
    const uint8_t sel = (uint8_t)(1u << MD_BTN_SELECT);
    if (pressed & sel) {
        s_mdSelSinceMs = now;
        s_mdSelLong = false;
    } else if ((down & sel) && !s_mdSelLong && now - s_mdSelSinceMs >= kMdHoldBackMs) {
        s_mdSelLong = true;
        input(RK_BACK);
    } else if (!(down & sel) && s_mdSelSinceMs && !s_mdSelLong) {
        input(RK_ENTER);
        s_mdSelSinceMs = 0;
    }
    if (!(down & sel)) s_mdSelSinceMs = 0;
#else
    (void)now;
#endif
}

}  // namespace

void keyboardBegin() {
    hwBegin();
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, readCb);
}

void keyboardPoll() {
#ifdef PLATY_SERIAL_KEYS
    // Debug builds: raw key codes (keys.h) typed on the serial console act as key presses.
    while (Serial.available() > 0) input((uint8_t)Serial.read());
#endif
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextPollMs) < 0) return;
    s_nextPollMs = now + 15;
    hwPoll(now);
}

void keyboardSetHotkeyHandler(KeyboardHotkeyHandler h) { s_hotkey = h; }

lv_indev_t *keyboardIndev() { return s_indev; }

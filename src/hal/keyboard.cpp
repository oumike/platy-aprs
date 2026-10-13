#include "keyboard.h"
#include "board_pins.h"
#include "keys.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#if defined(DEVICE_M9)
#include <soc/usb_serial_jtag_reg.h>
#elif defined(DEVICE_WIO_TRACKER_L2)
#include "wio_l2_io.h"
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

uint32_t mapKey(uint8_t k) {
    switch (k) {
        case RK_UP:    return focusedWantsArrows() ? LV_KEY_UP : LV_KEY_PREV;
        case RK_DOWN:  return focusedWantsArrows() ? LV_KEY_DOWN : LV_KEY_NEXT;
        case RK_LEFT:  return LV_KEY_LEFT;
        case RK_RIGHT: return LV_KEY_RIGHT;
        case RK_ENTER: return LV_KEY_ENTER;
        case RK_BKSP:  return LV_KEY_BACKSPACE;
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
bool s_tbClickDown = false;
uint32_t s_tbLastMoveMs = 0;
#endif

#if defined(DEVICE_WIO_TRACKER_L2)
bool s_btnDown = false, s_btnLong = false, s_wakeDown = false;
uint32_t s_btnSinceMs = 0, s_nextWakePollMs = 0;
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
#endif
}

void hwPoll(uint32_t now) {
#if defined(DEVICE_M9)
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
        push(k);
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
            if (abs(dx) > abs(dy)) push(dx > 0 ? RK_RIGHT : RK_LEFT);
            else push(dy > 0 ? RK_DOWN : RK_UP);
        }
    }
    // The ball's click switch chatters while rolling; ignore clicks right after motion.
    const bool click = digitalRead(PIN_TB_CLICK) == LOW;
    if (click && !s_tbClickDown && now - s_tbLastMoveMs > 160) push(RK_ENTER);
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
    push(k);
#elif defined(DEVICE_WIO_TRACKER_L2)
    // BOOT button: short press = Back, hold = Home.
    const bool down = digitalRead(PIN_USER_BUTTON) == LOW;
    if (down && !s_btnDown) {
        s_btnSinceMs = now;
        s_btnLong = false;
    } else if (down && !s_btnLong && now - s_btnSinceMs > 700) {
        s_btnLong = true;
        push(RK_HOME);
    } else if (!down && s_btnDown && !s_btnLong && now - s_btnSinceMs > 30) {
        push(RK_BACK);
    }
    s_btnDown = down;
    // Top wake button (on the expander): Home. Only read it when the expander
    // signals an input change.
    if (digitalRead(PIN_EXPANDER_INT) == LOW || s_wakeDown) {
        if ((int32_t)(now - s_nextWakePollMs) >= 0) {
            s_nextWakePollMs = now + 50;
            const bool w = wioIoWakePressed();
            if (w && !s_wakeDown) push(RK_HOME);
            s_wakeDown = w;
        }
    }
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
    while (Serial.available() > 0) push((uint8_t)Serial.read());
#endif
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextPollMs) < 0) return;
    s_nextPollMs = now + 15;
    hwPoll(now);
}

void keyboardSetHotkeyHandler(KeyboardHotkeyHandler h) { s_hotkey = h; }

lv_indev_t *keyboardIndev() { return s_indev; }

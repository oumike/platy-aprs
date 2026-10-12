#include "keyboard.h"
#include "pins_m9.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <soc/usb_serial_jtag_reg.h>

namespace {

uint8_t s_keyReg = KB_REG_KEY;
bool s_present = false;
uint32_t s_nextProbeMs = 0;
uint32_t s_nextPollMs = 0;

uint8_t s_ring[32];
uint8_t s_head = 0, s_tail = 0;

KeyboardHotkeyHandler s_hotkey = nullptr;
lv_indev_t *s_indev = nullptr;

// One pending key to report as PRESSED then RELEASED across two read_cb calls.
uint32_t s_pendingKey = 0;
bool s_pressedReported = false;

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
    return lv_obj_check_type(f, &lv_textarea_class) ||
           lv_obj_check_type(f, &lv_dropdown_class) ||
           lv_obj_check_type(f, &lv_roller_class) ||
           lv_obj_check_type(f, &lv_slider_class);
}

uint32_t mapKey(uint8_t k) {
    switch (k) {
        case M9_KEY_UP:    return focusedWantsArrows() ? LV_KEY_UP : LV_KEY_PREV;
        case M9_KEY_DOWN:  return focusedWantsArrows() ? LV_KEY_DOWN : LV_KEY_NEXT;
        case M9_KEY_LEFT:  return LV_KEY_LEFT;
        case M9_KEY_RIGHT: return LV_KEY_RIGHT;
        case M9_KEY_ENTER: return LV_KEY_ENTER;
        case M9_KEY_BKSP:  return LV_KEY_BACKSPACE;
        case M9_KEY_BACK:  return LV_KEY_ESC;
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

}  // namespace

void keyboardBegin() {
    // GPIO20 is the S3's USB D+ pad; the USB-Serial-JTAG PHY holds it until released.
    REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    delay(5);
    Wire1.begin(PIN_KB_SDA, PIN_KB_SCL, 100000UL);
    delay(50);
    probe();
    if (!s_present) Serial.println("[kb] controller not answering yet; will retry");

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, readCb);
}

void keyboardPoll() {
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextPollMs) < 0) return;
    s_nextPollMs = now + 15;
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
        push(k);
    }
}

void keyboardSetHotkeyHandler(KeyboardHotkeyHandler h) { s_hotkey = h; }

lv_indev_t *keyboardIndev() { return s_indev; }

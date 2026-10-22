// E-paper display driver (T-Deck Pro): GxEPD2 for the GDEQ031T10 panel, LVGL
// rendering native 1-bit (I1) frames, and the CST328 / CST3530 touch panel.
// Follows camillia-mt's tdeck_pro_display.h.
#include "board_pins.h"
#if HAS_EINK
#include "display.h"
#include "keyboard.h"
#include "screen.h"
#include <Arduino.h>
#include <CSE_CST328.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

constexpr int32_t kW = TFT_PANEL_W, kH = TFT_PANEL_H;
constexpr size_t kMonoStride = kW / 8;
constexpr size_t kMonoBytes = kMonoStride * kH;
constexpr size_t kPaletteBytes = 2 * sizeof(lv_color32_t);   // I1 buffers start with it
constexpr uint32_t kMinGapMs = 250;           // between refreshes
constexpr uint8_t kFullEvery = 10;            // partials before a full refresh clears ghosting
constexpr uint8_t kFullOnScreenChange = 3;    // a new screen gets a full refresh after this many partials
constexpr uint32_t kTypingQuietMs = 150;      // hold a refresh while keys are still coming
constexpr uint32_t kTypingMaxDeferMs = 1200;

GxEPD2_BW<GxEPD2_310_GDEQ031T10, 40> s_epd(
    GxEPD2_310_GDEQ031T10(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_EINK_BUSY));
CSE_CST328 s_cst328(kW, kH, &Wire, PIN_TOUCH_RST, PIN_TOUCH_INT);

lv_display_t *s_disp = nullptr;
uint8_t *s_mono = nullptr;       // what LVGL drew, 1 = white
uint8_t *s_sent = nullptr;       // what the panel shows
bool s_sentValid = false;
bool s_pending = false, s_fullPending = true;
bool s_sleeping = false;
uint32_t s_pendingSinceMs = 0, s_lastRefreshMs = 0;
uint8_t s_partials = 0;

void writeBrightness(uint8_t level) { ledcWrite(TFT_BL_PWM_CH, level); }

void requestRefresh(bool full) {
    if (!s_pending) s_pendingSinceMs = millis();
    s_pending = true;
    s_fullPending = s_fullPending || full;
}

// GxEPD2 spins on BUSY for ~0.7 s (partial) or ~1.1 s (full). The keyboard
// controller's FIFO holds five keystrokes, so it is drained throughout.
void busyCb(const void *) {
    keyboardServiceDuringRefresh();
    delay(1);
}

void serviceRefresh() {
    if (!s_pending || s_sleeping || !s_mono) return;
    const uint32_t now = millis();
    if (s_lastRefreshMs && now - s_lastRefreshMs < kMinGapMs) return;
    if (!s_fullPending && now - keyboardLastInputMs() < kTypingQuietMs &&
        now - s_pendingSinceMs < kTypingMaxDeferMs)
        return;
    const bool full = s_fullPending || s_partials >= kFullEvery;
    if (!full && s_sentValid && memcmp(s_mono, s_sent, kMonoBytes) == 0) {
        s_pending = false;
        return;
    }
    digitalWrite(PIN_LORA_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);
    if (full) s_epd.setFullWindow();
    else s_epd.setPartialWindow(0, 0, kW, kH);
    s_epd.firstPage();
    do {
        s_epd.drawInvertedBitmap(0, 0, s_mono, kW, kH, GxEPD_BLACK);
    } while (s_epd.nextPage());
    s_epd.powerOff();
    memcpy(s_sent, s_mono, kMonoBytes);
    s_sentValid = true;
    s_lastRefreshMs = millis();
    s_pending = false;
    s_fullPending = false;
    s_partials = full ? 0 : (uint8_t)(s_partials + 1);
}

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    const uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_I1);
    const uint8_t *src = px + kPaletteBytes;
    // LVGL rounds I1 areas to whole bytes, and full render mode flushes the screen.
    if (area->x1 >= 0 && area->y1 >= 0 && area->x2 < kW && area->y2 < kH && !(area->x1 & 7)) {
        const size_t rowBytes = ((size_t)w + 7) / 8;
        for (int32_t y = 0; y < h; y++)
            memcpy(s_mono + (size_t)(area->y1 + y) * kMonoStride + area->x1 / 8,
                   src + (size_t)y * stride, rowBytes);
    }
    if (lv_display_flush_is_last(disp)) requestRefresh(false);
    lv_display_flush_ready(disp);
}

uint32_t tickCb() { return millis(); }

void logCb(lv_log_level_t level, const char *buf) {
    (void)level;
    Serial.print(buf);
}

// ── Touch ───────────────────────────────────────────────────────────────────
bool s_touchReady = false, s_cst3530 = false;
volatile bool s_touchIrq = false;
void IRAM_ATTR touchIsr() { s_touchIrq = true; }

bool touchWrite(const uint8_t *b, size_t n) {
    Wire.beginTransmission((uint8_t)TOUCH_I2C_ADDR);
    Wire.write(b, n);
    return Wire.endTransmission() == 0;
}

bool cst3530Command(uint32_t c) {
    const uint8_t b[] = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
    return touchWrite(b, sizeof(b));
}

// Newer boards carry a CST3530 at the same address; it answers this query.
bool probeCst3530() {
    static const uint8_t kQuery[] = {0xD0, 0x03, 0x00, 0x00};
    for (int attempt = 0; attempt < 5; attempt++) {
        uint8_t r[7] = {};
        if (touchWrite(kQuery, sizeof(kQuery)) &&
            Wire.requestFrom((int)TOUCH_I2C_ADDR, (int)sizeof(r)) == (int)sizeof(r)) {
            Wire.readBytes(r, sizeof(r));
            if (r[2] == 0xCA && r[3] == 0xCA) return true;
        }
        cst3530Command(0xD0000400);   // wake
        delay(50);
    }
    return false;
}

bool initCst3530() {
    bool ok = cst3530Command(0xD0000400);
    delay(20);
    ok = cst3530Command(0xD0000400) && ok;
    delay(20);
    ok = cst3530Command(0xD0000000) && ok;
    ok = cst3530Command(0xD0000C00) && ok;
    return cst3530Command(0xD0000100) && ok;
}

bool readCst3530(int16_t &x, int16_t &y) {
    static const uint8_t kRead[] = {0xD0, 0x07, 0x00, 0x00};
    static const uint8_t kClear[] = {0xD0, 0x00, 0x02, 0xAB};
    uint8_t r[50] = {};
    if (!touchWrite(kRead, sizeof(kRead)) || Wire.requestFrom((int)TOUCH_I2C_ADDR, 9) != 9) return false;
    size_t got = Wire.readBytes(r, 9);
    const uint8_t fingers = r[3] & 0x0F, keys = (r[3] >> 4) & 0x0F;
    if (fingers + keys > 1) {
        size_t extra = (size_t)(fingers + keys - 1) * 5;
        if (extra > sizeof(r) - got) extra = sizeof(r) - got;
        if (Wire.requestFrom((int)TOUCH_I2C_ADDR, (int)extra) == (int)extra) got += Wire.readBytes(r + got, extra);
    }
    touchWrite(kClear, sizeof(kClear));
    if (fingers == 0 || (r[8] >> 4) == 0) return false;
    const size_t i = (size_t)keys * 5;
    if (i + 7 >= got) return false;
    x = (int16_t)(r[i + 4] | ((r[i + 7] & 0x0F) << 8));
    y = (int16_t)(r[i + 5] | ((r[i + 7] & 0xF0) << 4));
    return true;
}

void touchBegin() {
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(20);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(80);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(20);
    s_cst3530 = probeCst3530();
    if (s_cst3530) {
        pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
        s_touchReady = initCst3530();
        if (s_touchReady) attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), touchIsr, FALLING);
    } else {
        s_touchReady = s_cst328.begin();
        s_cst328.setRotation(0);
    }
    Serial.printf("[display] touch %s\n", s_touchReady ? (s_cst3530 ? "CST3530" : "CST328") : "missing");
}

bool readTouch(lv_point_t &pt) {
    if (!s_touchReady) return false;
    int16_t x, y;
    if (s_cst3530) {
        if (!s_touchIrq && digitalRead(PIN_TOUCH_INT) != LOW) return false;
        s_touchIrq = false;
        if (!readCst3530(x, y)) return false;
    } else {
        if (s_cst328.getTouches() == 0) return false;
        const CSE_TouchPoint p = s_cst328.getPoint(0);
        x = p.x;
        y = p.y;
    }
    if (x < 0 || y < 0 || x >= kW || y >= kH) return false;
    pt = {x, y};
    return true;
}

lv_indev_t *s_touch = nullptr;
uint32_t s_nextTouchMs = 0;
lv_point_t s_lastPt = {0, 0};
bool s_lastPressed = false;

// Double tap on the screensaver wakes the screen (as display.cpp).
constexpr uint32_t kTapMaxMs = 300;
constexpr uint32_t kTapGapMs = 400;
constexpr int32_t kTapSlopPx = 40;
struct {
    bool down;
    uint32_t downMs, upMs;
    lv_point_t downPt, firstPt;
    bool haveFirst;
} s_dt;

void saverDoubleTap(uint32_t now) {
    lv_point_t pt;
    const bool pressed = readTouch(pt);
    if (pressed && !s_dt.down) {
        s_dt.down = true;
        s_dt.downMs = now;
        s_dt.downPt = pt;
        if (s_dt.haveFirst && now - s_dt.upMs > kTapGapMs) s_dt.haveFirst = false;
    } else if (!pressed && s_dt.down) {
        s_dt.down = false;
        if (now - s_dt.downMs > kTapMaxMs) {
            s_dt.haveFirst = false;
            return;
        }
        if (s_dt.haveFirst && LV_ABS(s_dt.downPt.x - s_dt.firstPt.x) <= kTapSlopPx &&
            LV_ABS(s_dt.downPt.y - s_dt.firstPt.y) <= kTapSlopPx) {
            s_dt.haveFirst = false;
            screenTryWake("double tap");
            return;
        }
        s_dt.haveFirst = true;
        s_dt.firstPt = s_dt.downPt;
        s_dt.upMs = now;
    }
}

void touchCb(lv_indev_t *, lv_indev_data_t *data) {
    const uint32_t now = millis();
    if (screenAsleep()) {
        const bool watch = screenState() == ScreenState::Saver || !HAS_WAKE_BUTTON;
        if (watch && (int32_t)(now - s_nextTouchMs) >= 0) {
            s_nextTouchMs = now + 25;
            saverDoubleTap(now);
        } else if (!watch) {
            s_dt = {};
        }
        s_lastPressed = false;
        data->point = s_lastPt;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    s_dt = {};
    if ((int32_t)(now - s_nextTouchMs) >= 0) {
        s_nextTouchMs = now + 25;
        s_lastPressed = readTouch(s_lastPt);
    }
    if (s_lastPressed) screenNoteActivity();
    data->point = s_lastPt;
    data->state = s_lastPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

void displayBegin() {
    for (int pin : {PIN_TFT_CS, PIN_LORA_CS, PIN_SD_CS}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    s_mono = (uint8_t *)heap_caps_malloc(kMonoBytes, MALLOC_CAP_SPIRAM);
    s_sent = (uint8_t *)heap_caps_malloc(kMonoBytes, MALLOC_CAP_SPIRAM);
    if (!s_mono) s_mono = (uint8_t *)malloc(kMonoBytes);
    if (!s_sent) s_sent = (uint8_t *)malloc(kMonoBytes);
    if (s_mono) memset(s_mono, 0xFF, kMonoBytes);
    if (s_sent) memset(s_sent, 0xFF, kMonoBytes);

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    // 0: no GxEPD2 diagnostics (it would also call Serial.begin again).
    s_epd.init(0, true, 2, false, SPI, SPISettings(EINK_SPI_HZ, MSBFIRST, SPI_MODE0));
    s_epd.setRotation(0);
    s_epd.epd2.setBusyCallback(busyCb);

    ledcSetup(TFT_BL_PWM_CH, TFT_BL_FREQ, 8);
    ledcAttachPin(PIN_TFT_BL, TFT_BL_PWM_CH);
    writeBrightness(0);

    touchBegin();

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_log_register_print_cb(logCb);

    const size_t bytes = kPaletteBytes + lv_draw_buf_width_to_stride(kW, LV_COLOR_FORMAT_I1) * kH;
    void *buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, bytes, MALLOC_CAP_SPIRAM);
    memset(buf, 0xFF, bytes);

    s_disp = lv_display_create(kW, kH);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(s_disp, flushCb);
    // Full render mode: one flush per frame, and the panel is refreshed whole anyway.
    lv_display_set_buffers(s_disp, buf, nullptr, bytes, LV_DISPLAY_RENDER_MODE_FULL);
    s_touch = lv_indev_create();
    lv_indev_set_type(s_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch, touchCb);
    Serial.printf("[display] e-paper %dx%d, LVGL %d.%d.%d\n", (int)kW, (int)kH, LVGL_VERSION_MAJOR,
                  LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
}

// E-paper reads by ambient light, so the frontlight stays off and there's no
// brightness setting.
void displaySetBrightness(uint8_t) { writeBrightness(0); }

// The panel holds its image with no power: sleeping only darkens the
// frontlight and stops refreshes. Waking redraws it with a full refresh.
void displaySleep(bool sleep) {
    if (sleep == s_sleeping) return;
    s_sleeping = sleep;
    if (sleep) {
        writeBrightness(0);
        s_epd.powerOff();
    } else {
        requestRefresh(true);
    }
}

void displayLoop() {
    lv_timer_handler();
    serviceRefresh();
}

void displayScreenChanged() {
    if (s_partials >= kFullOnScreenChange) requestRefresh(true);
}

bool displayCanRotate() { return false; }

void displaySetOrientation(uint8_t) {}
#endif

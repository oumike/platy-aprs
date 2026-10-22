#include "display.h"
#include "board_pins.h"
#include "screen.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#if defined(DEVICE_WIO_TRACKER_L2)
#include <lgfx/v1/panel/Panel_NV3031B.hpp>
#elif defined(DEVICE_MESH_DECK)
#include "mesh_deck_io.h"
#endif
#include <lvgl.h>

namespace {

#if defined(DEVICE_WIO_TRACKER_L2)
// LP5814 4-channel I2C LED driver behind the panel (camillia-mt display.h).
class Light_LP5814 : public lgfx::ILight {
public:
    bool init(uint8_t brightness) override {
        bool ok = write(0x00, 0x01);            // chip enable
        ok = write(0x01, 0x01) && ok;           // max current 51 mA
        ok = write(0x02, 0x00) && ok;
        ok = write(0x04, 0x4E) && ok;           // dimming mode
        ok = write(0x05, 0xF0) && ok;           // manual mode
        for (uint8_t ch = 0; ch < 4; ch++) ok = write(0x14 + ch, 200) && ok;
        ok = write(0x02, 0x0F) && ok;           // enable all channels
        ok = write(0x0F, 0x55) && ok;           // latch
        delay(5);
        setBrightness(brightness);
        if (!ok) Serial.println("[display] LP5814 backlight not answering");
        return ok;
    }
    void setBrightness(uint8_t b) override {
        for (uint8_t ch = 0; ch < 4; ch++) write(0x18 + ch, b);
    }

private:
    static bool write(uint8_t reg, uint8_t v) {
        Wire.beginTransmission(LP5814_ADDR);
        Wire.write(reg);
        Wire.write(v);
        return Wire.endTransmission() == 0;
    }
};

class LGFX_Board : public lgfx::LGFX_Device {
    lgfx::Panel_NV3031B _panel;
    lgfx::Bus_SPI _bus;
    Light_LP5814 _light;
    lgfx::Touch_GT911 _touch;

public:
    // The backlight must be up before GT911 probing, and a NACK while probing
    // can wedge the I2C peripheral, so the bus is restarted afterwards.
    bool init_impl(bool use_reset, bool use_clear) override {
        _light.init(0);
        const bool ok = LGFX_Device::init_impl(use_reset, use_clear);
        Wire.end();
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
        return ok;
    }

    LGFX_Board() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 3;
            cfg.freq_write = 75000000;
            cfg.freq_read = 16000000;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_TFT_SCK;
            cfg.pin_mosi = -1;
            cfg.pin_miso = -1;
            cfg.pin_dc = -1;
            // Setting all four IO pins is what selects quad mode.
            cfg.pin_io0 = PIN_TFT_IO0;
            cfg.pin_io1 = PIN_TFT_IO1;
            cfg.pin_io2 = PIN_TFT_IO2;
            cfg.pin_io3 = PIN_TFT_IO3;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = TFT_PANEL_W;
            cfg.panel_height = TFT_PANEL_H;
            cfg.memory_width = TFT_PANEL_W;
            cfg.memory_height = TFT_PANEL_H;
            cfg.offset_rotation = 1;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            cfg.invert = true;
            cfg.rgb_order = true;
            cfg.readable = false;
            _panel.config(cfg);
        }
        _panel.setLight(&_light);
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_W - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_H - 1;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = TOUCH_OFFSET_ROT;
            cfg.i2c_port = 0;
            cfg.i2c_addr = TOUCH_ADDR;
            cfg.pin_sda = PIN_I2C_SDA;
            cfg.pin_scl = PIN_I2C_SCL;
            cfg.freq = I2C_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

#elif defined(DEVICE_MESH_DECK)

// ST7789 alone on SPI3, write-only. Touch is read over Wire in touchCb rather
// than by LovyanGFX (see mesh_deck_io.cpp).
class LGFX_Board : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX_Board() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_TFT_SCK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = TFT_PANEL_W;
            cfg.panel_height = TFT_PANEL_H;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.readable = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

#elif defined(DEVICE_CROWPANEL_35)

// ILI9488 alone on SPI3, write-only; GT911 on its own I2C pins, read by
// LovyanGFX, which rotates touch along with the panel (camillia-mt display.h).
class LGFX_Board : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_GT911 _touch;

public:
    LGFX_Board() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_TFT_SCK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = TFT_PANEL_W;
            cfg.panel_height = TFT_PANEL_H;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.readable = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = false;
            cfg.freq = 44000;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_W - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_H - 1;
            cfg.pin_int = PIN_TOUCH_INT;
            cfg.pin_rst = PIN_TOUCH_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 0;
            cfg.i2c_addr = TOUCH_ADDR;
            cfg.pin_sda = PIN_I2C_SDA;
            cfg.pin_scl = PIN_I2C_SCL;
            cfg.freq = I2C_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

#else  // ST7789 on a shared SPI bus with PWM backlight (M9, T-Deck)

class LGFX_Board : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
#if HAS_TOUCH
    lgfx::Touch_GT911 _touch;
#endif

public:
    LGFX_Board() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 1000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_SPI_SCK;
            cfg.pin_mosi = PIN_SPI_MOSI;
            cfg.pin_miso = PIN_SPI_MISO;
            cfg.pin_dc = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = TFT_PANEL_W;
            cfg.panel_height = TFT_PANEL_H;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.readable = true;
#if defined(DEVICE_TDECK)
            cfg.dummy_read_pixel = 16;
            cfg.end_read_delay_us = 8;
#endif
            cfg.bus_shared = true;   // SD card + radio share this bus
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
#if defined(DEVICE_M9)
            cfg.invert = true;   // PNP: LOW = on
#else
            cfg.invert = false;
#endif
            cfg.freq = 12000;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
#if HAS_TOUCH
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_W - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_H - 1;
            cfg.pin_int = PIN_TOUCH_INT;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 0;
            cfg.i2c_addr = TOUCH_ADDR;
            cfg.pin_sda = PIN_I2C_SDA;
            cfg.pin_scl = PIN_I2C_SCL;
            cfg.freq = I2C_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
#endif
        setPanel(&_panel);
    }
};

#endif

LGFX_Board lcd;

constexpr int kBufLines = 30;

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels(reinterpret_cast<uint16_t *>(px), w * h, true);
    lcd.endWrite();
    lv_display_flush_ready(disp);
}

uint32_t tickCb() { return millis(); }

#if HAS_TOUCH
lv_indev_t *s_touch = nullptr;
uint32_t s_nextTouchMs = 0;
lv_point_t s_lastPt = {0, 0};
bool s_lastPressed = false;

bool readTouch(lv_point_t &pt) {
#if defined(DEVICE_MESH_DECK)
    // Native portrait -> rotation 3 landscape.
    int16_t x, y;
    if (!meshDeckReadTouch(x, y)) return false;
    pt = {(int32_t)(TFT_PANEL_H - 1 - y), (int32_t)x};
    return true;
#else
    lgfx::touch_point_t tp;
    if (lcd.getTouch(&tp, 1) <= 0) return false;
    pt = {tp.x, tp.y};
    return true;
#endif
}

// Double tap on the screensaver wakes the screen: two short taps close
// together in time and place, so a brush from a pocket or sleeve doesn't.
// It fires on the second release, so neither tap reaches the UI. A board with
// no wake button (HAS_WAKE_BUTTON 0) takes it on a dark screen as well.
constexpr uint32_t kTapMaxMs = 300;      // finger down at most this long
constexpr uint32_t kTapGapMs = 400;      // release to the next press
constexpr int32_t kTapSlopPx = 40;       // the two taps this close
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
    // GT911 polling over I2C is slow; 25 ms is plenty for UI work.
    // Touch never reaches the UI while locked (camillia-mt's policy: the panel
    // reacts to pockets and sleeves). Only the double-tap wake is watched: on
    // the screensaver, and on a dark panel where there is no wake button.
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
#endif

void logCb(lv_log_level_t level, const char *buf) {
    (void)level;
    Serial.print(buf);
}

}  // namespace

void displayBegin() {
    lcd.init();
    lcd.setRotation(TFT_ROTATION);
    lcd.setBrightness(160);
    lcd.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_log_register_print_cb(logCb);

    const int w = lcd.width();
    const int h = lcd.height();
    const size_t bytes = (size_t)w * kBufLines * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    lv_display_t *disp = lv_display_create(w, h);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flushCb);
    lv_display_set_buffers(disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
#if HAS_TOUCH
    s_touch = lv_indev_create();
    lv_indev_set_type(s_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch, touchCb);
#endif
    Serial.printf("[display] %dx%d, LVGL %d.%d.%d\n", w, h, LVGL_VERSION_MAJOR,
                  LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
}

void displaySetBrightness(uint8_t level) { lcd.setBrightness(level); }

void displaySleep(bool sleep) {
    if (sleep) lcd.sleep();
    else lcd.wakeup();
}

void displayLoop() { lv_timer_handler(); }

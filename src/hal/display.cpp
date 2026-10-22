#include "board_pins.h"
#if !HAS_EINK
#include "display.h"
#include "config/config.h"
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
#elif defined(DEVICE_CROWPANEL_50)
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#endif
#include <lvgl.h>

#ifndef TFT_OFFSET_X
#define TFT_OFFSET_X 0
#endif

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

#elif defined(DEVICE_CROWPANEL_50)

// The backlight and the GT911 reset belong to a PCA9557 (V1.0) or a board MCU
// (V1.1+); begin() works out which by who answers (camillia-mt
// crowpanel_50_display.h).
class CrowPanelCtl {
public:
    enum Kind : uint8_t { NONE, PCA9557, MCU };

    void begin() {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
        if (probe(CROWPANEL_IOEX_ADDR)) {
            _kind = PCA9557;
            // Levels first, then direction: backlight on, touch out of reset,
            // amplifier held off, as Elecrow starts it.
            _out = pinMask(CROWPANEL_IOEX_BL) | pinMask(CROWPANEL_IOEX_TP_RST) | pinMask(CROWPANEL_IOEX_AMP_SHUT);
            pcaWrite(0x01, _out);
            pcaWrite(0x03, (uint8_t)~(pinMask(CROWPANEL_IOEX_BL) | pinMask(CROWPANEL_IOEX_TP_RST) |
                                      pinMask(CROWPANEL_IOEX_AMP_MUTE) | pinMask(CROWPANEL_IOEX_AMP_SHUT)));
            Serial.println("[display] CrowPanel V1.0 (PCA9557)");
#if !CROWPANEL_50_HW_V10
            Serial.println("[display] V1.0 board: the radio needs a -DCROWPANEL_50_HW_V10=1 build");
#endif
        } else if (probe(CROWPANEL_MCU_ADDR)) {
            _kind = MCU;
            Serial.println("[display] CrowPanel V1.1+ (board MCU)");
        } else {
            Serial.println("[display] CrowPanel: no backlight controller at 0x18 or 0x30");
        }
    }

    // GT911 address strap: INT held low through the reset gives 0x5D. The MCU's
    // wake command is repeated until the GT911 answers, as Elecrow does.
    // Returns the address it answered at, 0 if none.
    uint8_t resetTouch() {
        if (_kind == NONE) return 0;
        for (int attempt = 0; attempt < 5; attempt++) {
            pinMode(PIN_TOUCH_INT, OUTPUT);
            digitalWrite(PIN_TOUCH_INT, LOW);
            if (_kind == PCA9557) {
                pcaSet(CROWPANEL_IOEX_TP_RST, false);
                delay(20);
                pcaSet(CROWPANEL_IOEX_TP_RST, true);
                delay(100);
            } else {
                mcuSend(CROWPANEL_MCU_TOUCH_ON);
                delay(120);
            }
            pinMode(PIN_TOUCH_INT, INPUT);
            delay(100);
            if (probe(0x5D)) return 0x5D;
            if (probe(0x14)) return 0x14;
        }
        Serial.println("[display] GT911 not answering: no touch");
        return 0;
    }

    // 0 is off. V1.0's backlight only switches; the MCU dims.
    void setBacklight(uint8_t level) {
        if (_kind == PCA9557) {
            pcaSet(CROWPANEL_IOEX_BL, level > 0);
        } else if (_kind == MCU) {
            const uint8_t span = CROWPANEL_MCU_BL_MIN - CROWPANEL_MCU_BL_MAX;
            const uint8_t cmd = level == 0 ? CROWPANEL_MCU_BL_OFF
                                           : (uint8_t)(CROWPANEL_MCU_BL_MIN - (uint32_t)(level - 1) * span / 254);
            if (cmd != _lastMcu && mcuSend(cmd)) _lastMcu = cmd;
        }
    }

private:
    static uint8_t pinMask(uint8_t pin) { return (uint8_t)(1u << pin); }
    static bool probe(uint8_t addr) {
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    }
    void pcaSet(uint8_t pin, bool high) {
        const uint8_t next = high ? (uint8_t)(_out | pinMask(pin)) : (uint8_t)(_out & ~pinMask(pin));
        if (next == _out) return;
        _out = next;
        pcaWrite(0x01, _out);
    }
    static void pcaWrite(uint8_t reg, uint8_t v) {
        Wire.beginTransmission(CROWPANEL_IOEX_ADDR);
        Wire.write(reg);
        Wire.write(v);
        Wire.endTransmission();
    }
    static bool mcuSend(uint8_t cmd) {
        Wire.beginTransmission(CROWPANEL_MCU_ADDR);
        Wire.write(cmd);
        return Wire.endTransmission() == 0;
    }
    Kind _kind = NONE;
    uint8_t _out = 0;
    int16_t _lastMcu = -1;
};

class Light_CrowPanel : public lgfx::ILight {
public:
    explicit Light_CrowPanel(CrowPanelCtl &ctl) : _ctl(ctl) {}
    bool init(uint8_t b) override {
        setBrightness(b);
        return true;
    }
    void setBrightness(uint8_t b) override { _ctl.setBacklight(b); }

private:
    CrowPanelCtl &_ctl;
};

// 800x480 RGB565 scanned out of a PSRAM framebuffer by the S3's LCD_CAM.
// Bus pins and timings are Elecrow's factory code, as camillia-mt and
// wadamesh run them.
class LGFX_Board : public lgfx::LGFX_Device {
    lgfx::Bus_RGB _bus;
    lgfx::Panel_RGB _panel;
    CrowPanelCtl _ctl;
    Light_CrowPanel _light{_ctl};
    lgfx::Touch_GT911 _touch;

public:
    bool init_impl(bool use_reset, bool use_clear) override {
        _ctl.begin();
        const uint8_t addr = _ctl.resetTouch();
        if (addr) {
            auto cfg = _touch.config();
            cfg.i2c_addr = addr;
            _touch.config(cfg);
        }
        return LGFX_Device::init_impl(use_reset, use_clear);
    }

    LGFX_Board() {
        {
            auto cfg = _panel.config();
            cfg.memory_width = cfg.panel_width = TFT_PANEL_W;
            cfg.memory_height = cfg.panel_height = TFT_PANEL_H;
            cfg.offset_x = cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.readable = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _panel.config_detail();
            cfg.use_psram = 1;   // 750 KB
            _panel.config_detail(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;
            cfg.pin_d0 = GPIO_NUM_21;    // B0
            cfg.pin_d1 = GPIO_NUM_47;
            cfg.pin_d2 = GPIO_NUM_48;
            cfg.pin_d3 = GPIO_NUM_45;
            cfg.pin_d4 = GPIO_NUM_38;
            cfg.pin_d5 = GPIO_NUM_9;     // G0
            cfg.pin_d6 = GPIO_NUM_10;
            cfg.pin_d7 = GPIO_NUM_11;
            cfg.pin_d8 = GPIO_NUM_12;
            cfg.pin_d9 = GPIO_NUM_13;
            cfg.pin_d10 = GPIO_NUM_14;
            cfg.pin_d11 = GPIO_NUM_7;    // R0
            cfg.pin_d12 = GPIO_NUM_17;
            cfg.pin_d13 = GPIO_NUM_18;
            cfg.pin_d14 = GPIO_NUM_3;
            cfg.pin_d15 = GPIO_NUM_46;
            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync = GPIO_NUM_41;
            cfg.pin_hsync = GPIO_NUM_40;
            cfg.pin_pclk = GPIO_NUM_39;
            cfg.freq_write = CROWPANEL_PCLK_HZ;
            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 8;
            cfg.pclk_idle_high = 1;   // 0 darkens single-pixel detail
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);
        _panel.setLight(&_light);
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_W - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_H - 1;
            cfg.pin_int = -1;   // polled; INT is only the address strap
            cfg.pin_rst = -1;   // on the board controller
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
        setPanel(&_panel);
    }
};

#else  // ST7789 (ST7796 on the Pager) on a shared SPI bus with PWM backlight (M9, T-Deck, Pager)

class LGFX_Board : public lgfx::LGFX_Device {
#if defined(DEVICE_TLORA_PAGER)
    lgfx::Panel_ST7796 _panel;
#else
    lgfx::Panel_ST7789 _panel;
#endif
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
            cfg.offset_x = TFT_OFFSET_X;
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
lv_display_t *s_disp = nullptr;

// Panel rotation for an Orientation; TFT_ROTATION is the board's landscape.
uint8_t panelRotation(uint8_t o) {
    static const uint8_t add[] = {0, 2, 1, 3};
    return (TFT_ROTATION + (HAS_ROTATION ? add[o & 3] : 0)) & 3;
}

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
    lcd.setRotation(panelRotation((uint8_t)g_config.orientation));
    lcd.setBrightness(160);
    lcd.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_log_register_print_cb(logCb);

    const int w = lcd.width();
    const int h = lcd.height();
    // Sized for the longer side, so a later rotation still fits.
    const size_t bytes = (size_t)(w > h ? w : h) * kBufLines * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    lv_display_t *disp = lv_display_create(w, h);
    s_disp = disp;
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

bool displayCanRotate() { return HAS_ROTATION; }

void displaySetOrientation(uint8_t o) {
    lcd.setRotation(panelRotation(o));
    lcd.fillScreen(TFT_BLACK);
    lv_display_set_resolution(s_disp, lcd.width(), lcd.height());
    Serial.printf("[display] orientation %u: %dx%d\n", o, lcd.width(), lcd.height());
}

void displayScreenChanged() {}
#endif

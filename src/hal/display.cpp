#include "display.h"
#include "pins_m9.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>

namespace {

class LGFX_M9 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX_M9() {
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
            cfg.bus_shared = true;   // SD card + LR1110 share this bus
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = true;   // PNP: LOW = on
            cfg.freq = 12000;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

LGFX_M9 lcd;

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
    Serial.printf("[display] %dx%d, LVGL %d.%d.%d\n", w, h, LVGL_VERSION_MAJOR,
                  LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
}

void displaySetBrightness(uint8_t level) { lcd.setBrightness(level); }

void displayLoop() { lv_timer_handler(); }

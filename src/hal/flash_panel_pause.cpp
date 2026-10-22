// CrowPanel 5.0: flash and the octal PSRAM share one bus. While a flash write
// or erase (a config save, OTA) holds it, the RGB panel's DMA can't read the
// framebuffer and the picture jumps sideways until the next frame. This
// Arduino core has neither bounce buffers nor flash auto-suspend, so the LCD
// clock is stopped for each flash operation and the panel just holds its
// image (wadamesh variants/crowpanel_50/FlashPanelPause.cpp). The env links
// it in with -Wl,--wrap=esp_flash_write -Wl,--wrap=esp_flash_erase_region.
#if defined(DEVICE_CROWPANEL_50)
#include <Arduino.h>
#include <esp_flash.h>
#include <soc/lcd_cam_struct.h>

namespace {
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
int s_depth = 0;
uint32_t s_saved = 0;

void lcdPause() {
    portENTER_CRITICAL(&s_mux);
    if (s_depth++ == 0) {
        s_saved = LCD_CAM.lcd_clock.val;
        LCD_CAM.lcd_clock.lcd_clk_sel = 0;
    }
    portEXIT_CRITICAL(&s_mux);
}

void lcdResume() {
    portENTER_CRITICAL(&s_mux);
    if (--s_depth == 0) LCD_CAM.lcd_clock.val = s_saved;
    portEXIT_CRITICAL(&s_mux);
}
}  // namespace

extern "C" {
esp_err_t __real_esp_flash_write(esp_flash_t *, const void *, uint32_t, uint32_t);
esp_err_t __real_esp_flash_erase_region(esp_flash_t *, uint32_t, uint32_t);

esp_err_t __wrap_esp_flash_write(esp_flash_t *chip, const void *buf, uint32_t addr, uint32_t len) {
    lcdPause();
    const esp_err_t r = __real_esp_flash_write(chip, buf, addr, len);
    lcdResume();
    return r;
}

esp_err_t __wrap_esp_flash_erase_region(esp_flash_t *chip, uint32_t addr, uint32_t len) {
    lcdPause();
    const esp_err_t r = __real_esp_flash_erase_region(chip, addr, len);
    lcdResume();
    return r;
}
}
#endif

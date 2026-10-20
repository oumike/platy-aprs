#include "screen.h"
#include "config/config.h"
#include "display.h"
#include <Arduino.h>
#include <lvgl.h>

const uint32_t kScreenTimeoutSecs[] = {15, 30, 60, 120, 300, 600, 1800, 3600, 0};
const int kScreenTimeoutCount = sizeof(kScreenTimeoutSecs) / sizeof(kScreenTimeoutSecs[0]);

namespace {

// Timings from camillia-mt.
constexpr uint32_t kWakeInputDelayMs = 3000;   // post-sleep guard
constexpr uint32_t kHoldMs = 2000;             // wake-button hold
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kPreSleepDimMs = 10000;
constexpr uint8_t kPreSleepDimPct = 25;        // of the configured level
constexpr uint32_t kPanelWakeSettleMs = 120;

bool s_asleep = false;
bool s_dimmed = false;
uint32_t s_lastActivityMs = 0;
uint32_t s_wakeBlockedUntilMs = 0;

}  // namespace

const char *screenTimeoutLabel(uint32_t secs) {
    switch (secs) {
        case 0: return "Never";
        case 15: return "15 sec";
        case 30: return "30 sec";
        case 60: return "1 min";
        case 120: return "2 min";
        case 300: return "5 min";
        case 600: return "10 min";
        case 1800: return "30 min";
        case 3600: return "60 min";
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "%lu sec", (unsigned long)secs);
            return buf;
        }
    }
}

void screenBegin() { s_lastActivityMs = millis(); }

bool screenAsleep() { return s_asleep; }

void screenNoteActivity() {
    s_lastActivityMs = millis();
    if (s_dimmed && !s_asleep) {
        s_dimmed = false;
        displaySetBrightness(g_config.brightness);
    }
}

void screenApplyBrightness() {
    if (!s_asleep) displaySetBrightness(g_config.brightness);
    s_dimmed = false;
}

void screenSleep(const char *reason) {
    if (s_asleep) return;
    // SLPIN stops the panel's own refresh; a dark backlight alone would leave
    // the controller powered and refreshing an image nobody can see.
    displaySleep(true);
    s_asleep = true;
    s_dimmed = false;
    s_wakeBlockedUntilMs = millis() + kWakeInputDelayMs;
    Serial.printf("[screen] sleeping (%s)\n", reason ? reason : "");
}

bool screenTryWake(const char *reason) {
    const uint32_t now = millis();
    if (!s_asleep) {
        s_lastActivityMs = now;
        return true;
    }
    if ((int32_t)(now - s_wakeBlockedUntilMs) < 0) return false;
    // SLPOUT with the backlight still dark, repaint, then light it: the panel
    // still holds the frame from before it slept.
    displaySleep(false);
    displaySetBrightness(0);
    delay(kPanelWakeSettleMs);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
    displaySetBrightness(g_config.brightness);
    s_asleep = false;
    s_dimmed = false;
    s_lastActivityMs = millis();
    Serial.printf("[screen] awake (%s)\n", reason ? reason : "");
    return true;
}

bool screenServiceWakeButton(WakeButton &b, bool pressed, uint32_t now, const char *name) {
    bool acted = false;
    if (pressed != b.raw) {
        b.raw = pressed;
        b.changedMs = now;
    }
    if (b.stable != b.raw && now - b.changedMs >= kDebounceMs) {
        b.stable = b.raw;
        if (b.stable) {
            b.downMs = now;
            b.claimed = false;
        } else {
            // A tap is only known on release: until then it may still become a hold.
            const bool tap = !b.claimed && b.downMs && now - b.downMs < kHoldMs;
            b.downMs = 0;
            b.claimed = false;
            if (tap) {
                // Awake: put it away. Dark: a tap is what a pocket does, so nothing.
                if (!s_asleep) screenSleep(name);
                acted = true;
            }
        }
    }
    if (b.stable && !b.claimed && b.downMs && now - b.downMs >= kHoldMs) {
        // Dark: wake. Awake: claimed anyway, so resting on the button does not
        // turn the screen off on release. Not latched while the post-sleep
        // guard refuses, so a button kept down wakes as soon as it expires.
        if (!s_asleep) {
            b.claimed = true;
            s_lastActivityMs = now;
        } else {
            b.claimed = screenTryWake(name);
        }
    }
    return acted || b.stable;
}

void screenLoop() {
    if (s_asleep || g_config.screenOnSecs == 0) return;
    const uint32_t idle = millis() - s_lastActivityMs;
    const uint32_t timeout = g_config.screenOnSecs * 1000UL;
    if (idle > timeout) {
        screenSleep("timeout");
    } else if (!s_dimmed && timeout > kPreSleepDimMs && idle > timeout - kPreSleepDimMs) {
        // Step the backlight down for the tail of the timeout, as a warning.
        uint8_t dim = (uint8_t)((uint32_t)g_config.brightness * kPreSleepDimPct / 100U);
        displaySetBrightness(dim ? dim : 1);
        s_dimmed = true;
    }
}

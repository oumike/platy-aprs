#include "notify_led.h"
#include "hal/board_pins.h"
#include "services/messages.h"
#if HAS_NOTIFY_LED && defined(DEVICE_MESH_DECK)
#include "hal/mesh_deck_io.h"
#endif
#if HAS_KB_BLINK
#include "hal/keyboard.h"
#include "hal/screen.h"
#endif

namespace {

// camillia-mt's pattern: two short flashes, repeated each second.
constexpr uint8_t kBlinks = 2;
constexpr uint32_t kOnMs = 60, kOffMs = 90, kRepeatMs = 1000;

LedColor s_color = LedColor::Off;
uint8_t s_phasesLeft = 0;      // on and off halves left in the current pattern
uint32_t s_nextMs = 0, s_patternMs = 0;
uint32_t s_armedMs = 0;        // when the last message arrived; 0 = not armed
bool s_lit = false;
bool s_preview = false;        // a Settings preview runs even with the screen on

void setLight(LedColor c, bool on) {
#if HAS_NOTIFY_LED && defined(DEVICE_MESH_DECK)
    bool r = false, g = false, b = false;
    if (on) {
        switch (c) {
            case LedColor::Red: r = true; break;
            case LedColor::Green: g = true; break;
            case LedColor::Blue: b = true; break;
            case LedColor::Yellow: r = g = true; break;
            case LedColor::Cyan: g = b = true; break;
            case LedColor::Magenta: r = b = true; break;
            case LedColor::White: r = g = b = true; break;
            default: break;
        }
    }
    meshDeckSetLed(r, g, b);
#elif HAS_KB_BLINK
    (void)c;
    keyboardSetBacklight(on ? 255 : 0);   // rests dark
#else
    (void)c;
    (void)on;
#endif
}

bool enabled() {
#if HAS_KB_BLINK
    return g_config.kbBlink;
#else
    return g_config.ledColor != LedColor::Off;
#endif
}

LedColor blinkColor() { return HAS_KB_BLINK ? LedColor::White : g_config.ledColor; }

// The keyboard blinks only while the screen is off or in the screensaver.
bool screenAllows() {
#if HAS_KB_BLINK
    return screenState() != ScreenState::On;
#else
    return true;
#endif
}

void startPattern(LedColor c) {
    s_color = c;
    s_phasesLeft = kBlinks * 2;
    s_nextMs = 0;
    s_patternMs = millis();
}

// Ends a pattern early and leaves the light dark.
void release() {
    if (s_lit || s_phasesLeft) setLight(s_color, false);
    s_lit = false;
    s_phasesLeft = 0;
    s_preview = false;
}

bool windowOpen(uint32_t now) {
    if (!s_armedMs) return false;
    if (!g_config.ledTimeoutSecs) return true;
    return now - s_armedMs < (uint32_t)g_config.ledTimeoutSecs * 1000UL;
}

}  // namespace

bool notifyLedPresent() { return HAS_NOTIFY_LED; }
bool notifyKbBlinkPresent() { return HAS_KB_BLINK; }

void notifyLedMessage() {
    if (!notifyLightPresent() || !enabled()) return;
    s_armedMs = millis() | 1;
    if (!s_phasesLeft && screenAllows()) startPattern(blinkColor());
}

void notifyLedPreview(LedColor c) {
    if (!notifyLightPresent()) return;
    release();
    if (c == LedColor::Off) return;
    s_preview = true;
    startPattern(c);
}

void notifyLedApplySetting() {
    if (!s_preview) release();
}

void notifyLedLoop() {
    if (!notifyLightPresent()) return;
    const uint32_t now = millis();
    if (!s_preview) {
        if (!enabled()) {
            if (s_lit || s_phasesLeft) release();
            s_armedMs = 0;
            return;
        }
        if (!screenAllows()) {
            // The screen came on: whatever arrived has been seen.
            if (s_lit || s_phasesLeft) release();
            s_armedMs = 0;
            return;
        }
    }
    if (!s_phasesLeft) {
        if (s_preview) {
            s_preview = false;
            return;
        }
        // Idle: repeat while something is unread and the timeout hasn't run
        // out. A pattern already running always finishes, so it ends dark.
        if (!windowOpen(now)) return;
        if (messagesUnread() == 0) {
            s_armedMs = 0;
            return;
        }
        if (now - s_patternMs < kRepeatMs) return;
        startPattern(blinkColor());
    }
    if (s_nextMs && (int32_t)(now - s_nextMs) < 0) return;
    s_lit = !s_lit;
    setLight(s_color, s_lit);
    s_nextMs = now + (s_lit ? kOnMs : kOffMs);
    if (--s_phasesLeft == 0 && s_lit) {
        s_lit = false;
        setLight(s_color, false);
    }
}

const char *ledColorLabel(LedColor c) {
    static const char *const kLabels[kLedColorCount] = {"Off", "Red", "Green", "Blue",
                                                         "Yellow", "Cyan", "Magenta", "White"};
    return (uint8_t)c < kLedColorCount ? kLabels[(uint8_t)c] : "Off";
}

String ledTimeoutLabel(uint16_t secs) {
    if (!secs) return "Never";
    if (secs < 60) return String(secs) + " s";
    return String(secs / 60) + " min";
}

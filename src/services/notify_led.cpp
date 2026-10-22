#include "notify_led.h"
#include "hal/board_pins.h"
#include "services/messages.h"
#if HAS_NOTIFY_LED && defined(DEVICE_MESH_DECK)
#include "hal/mesh_deck_io.h"
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

void setLed(LedColor c, bool on) {
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
#else
    (void)c;
    (void)on;
#endif
}

void startPattern(LedColor c) {
    s_color = c;
    s_phasesLeft = kBlinks * 2;
    s_nextMs = 0;
    s_patternMs = millis();
}

bool windowOpen(uint32_t now) {
    if (!s_armedMs) return false;
    if (!g_config.ledTimeoutSecs) return true;
    return now - s_armedMs < (uint32_t)g_config.ledTimeoutSecs * 1000UL;
}

}  // namespace

bool notifyLedPresent() { return HAS_NOTIFY_LED; }

void notifyLedMessage() {
    if (!HAS_NOTIFY_LED || g_config.ledColor == LedColor::Off) return;
    s_armedMs = millis() | 1;
    if (!s_phasesLeft) startPattern(g_config.ledColor);
}

void notifyLedPreview(LedColor c) {
    if (!HAS_NOTIFY_LED) return;
    if (s_lit) setLed(s_color, false);
    s_lit = false;
    if (c == LedColor::Off) {
        s_phasesLeft = 0;
        return;
    }
    startPattern(c);
}

void notifyLedLoop() {
    if (!HAS_NOTIFY_LED) return;
    const uint32_t now = millis();
    if (!s_phasesLeft) {
        // Idle: repeat while something is unread and the timeout hasn't run
        // out. A pattern already running always finishes, so it ends dark.
        if (!windowOpen(now)) return;
        if (g_config.ledColor == LedColor::Off || messagesUnread() == 0) {
            s_armedMs = 0;
            return;
        }
        if (now - s_patternMs < kRepeatMs) return;
        startPattern(g_config.ledColor);
    }
    if (s_nextMs && (int32_t)(now - s_nextMs) < 0) return;
    s_lit = !s_lit;
    setLed(s_color, s_lit);
    s_nextMs = now + (s_lit ? kOnMs : kOffMs);
    if (--s_phasesLeft == 0 && s_lit) {
        s_lit = false;
        setLed(s_color, false);
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

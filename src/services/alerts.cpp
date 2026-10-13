#include "alerts.h"
#include "config/config.h"
#include "hal/board_pins.h"
#include <Arduino.h>

namespace {
struct Note {
    uint16_t hz;   // 0 = rest
    uint16_t ms;
};
const Note kMessage[] = {{1760, 90}, {0, 60}, {2349, 90}, {0, 60}, {2960, 140}, {0, 0}};
const Note kAck[] = {{2637, 50}, {0, 0}};
const Note kFailed[] = {{880, 180}, {0, 60}, {660, 260}, {0, 0}};
const Note kBeacon[] = {{1976, 25}, {0, 0}};

const Note *s_seq = nullptr;
uint32_t s_noteEndMs = 0;
}  // namespace

void alertPlay(Alert a) {
    if (!HAS_BUZZER || !g_config.buzzer) return;
    switch (a) {
        case Alert::Message: s_seq = kMessage; break;
        case Alert::Ack: s_seq = kAck; break;
        case Alert::Failed: s_seq = kFailed; break;
        case Alert::Beacon: s_seq = kBeacon; break;
    }
    s_noteEndMs = 0;
    alertsLoop();
}

void alertsLoop() {
#if HAS_BUZZER
    if (!s_seq || (s_noteEndMs && (int32_t)(millis() - s_noteEndMs) < 0)) return;
    if (s_seq->ms == 0) {
        noTone(PIN_BUZZER);
        s_seq = nullptr;
        s_noteEndMs = 0;
        return;
    }
    if (s_seq->hz) tone(PIN_BUZZER, s_seq->hz);
    else noTone(PIN_BUZZER);
    s_noteEndMs = millis() + s_seq->ms;
    if (!s_noteEndMs) s_noteEndMs = 1;
    s_seq++;
#endif
}

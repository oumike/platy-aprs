#include "alerts.h"
#include "hal/audio.h"
#include "hal/board_pins.h"
#include "notify_led.h"

namespace {
#define SEQ(name, ...) const Tone name[] = {__VA_ARGS__}

// Patterns per output: a piezo is loudest around 2-4 kHz, while the speakers
// sound shrill up there. The Chirpy and Bass shapes come from camillia-mt.
#if HAS_BUZZER
SEQ(kMsgDefault, {1760, 90}, {0, 60}, {2349, 90}, {0, 60}, {2960, 140});
SEQ(kMsgChirpy, {2637, 28}, {0, 22}, {3136, 28}, {0, 22}, {3951, 46});
SEQ(kMsgBass, {1109, 90}, {0, 60}, {880, 150});
SEQ(kAck, {2637, 50});
SEQ(kFailed, {880, 180}, {0, 60}, {660, 260});
SEQ(kBeacon, {1976, 25});
SEQ(kBoot, {2000, 60});
#elif defined(DEVICE_TDECK)
SEQ(kMsgDefault, {1760, 34}, {0, 10}, {1480, 34}, {0, 10}, {1320, 56});
SEQ(kMsgChirpy, {2093, 20}, {0, 6}, {2637, 20}, {0, 6}, {3136, 22}, {0, 6}, {2637, 34});
SEQ(kMsgBass, {784, 54}, {0, 12}, {659, 50}, {0, 12}, {523, 74});
SEQ(kAck, {1760, 40});
SEQ(kFailed, {660, 160}, {0, 50}, {494, 220});
SEQ(kBeacon, {1320, 25});
SEQ(kBoot, {1320, 50}, {0, 10}, {1760, 60});
#else
SEQ(kMsgDefault, {880, 68}, {0, 8}, {740, 68}, {0, 8}, {660, 112});
SEQ(kMsgChirpy, {1047, 36}, {0, 6}, {1319, 36}, {0, 6}, {1568, 40}, {0, 6}, {1319, 70});
SEQ(kMsgBass, {392, 92}, {0, 10}, {330, 86}, {0, 10}, {262, 130});
SEQ(kAck, {1319, 50});
SEQ(kFailed, {440, 180}, {0, 60}, {330, 260});
SEQ(kBeacon, {988, 30});
SEQ(kBoot, {660, 60}, {0, 10}, {880, 80});
#endif
#undef SEQ

template <size_t N>
void play(const Tone (&seq)[N]) {
    audioPlay(seq, N, g_config.volumePct);
}

void playMessage(AlertSound style) {
    switch (style) {
        case AlertSound::Chirpy: play(kMsgChirpy); break;
        case AlertSound::Bass: play(kMsgBass); break;
        case AlertSound::Off: break;
        default: play(kMsgDefault); break;
    }
}
}  // namespace

void alertPlay(Alert a) {
    if (!audioPresent() || g_config.alertSound == AlertSound::Off) return;
    switch (a) {
        case Alert::Message: playMessage(g_config.alertSound); break;
        case Alert::Ack: play(kAck); break;
        case Alert::Failed: play(kFailed); break;
        case Alert::Beacon: play(kBeacon); break;
        case Alert::Boot: play(kBoot); break;
    }
}

void alertPreview(AlertSound style) {
    if (audioPresent()) playMessage(style);
}

const char *alertSoundLabel(AlertSound s) {
    switch (s) {
        case AlertSound::Chirpy: return "Chirpy";
        case AlertSound::Bass: return "Bass";
        case AlertSound::Off: return "Off";
        default: return "Default";
    }
}

void alertsLoop() {
    audioLoop();
    notifyLedLoop();
}

#pragma once
#include <stdint.h>

// Screen power: the idle timeout, sleep/wake, and the board wake buttons.
// Behaviour follows camillia-mt (without its lock screen):
//   - after g_config.screenOnSecs with no input the panel dims for its last
//     10 s, then sleeps (panel SLPIN, backlight off); 0 = never
//   - input that arrives while asleep never reaches the UI; only each board's
//     wake gesture brings the panel back, and not in the first 3 s after it
//     went dark (so the press that turned it off cannot turn it on again)
//   - a board's wake button: tap = screen off, hold 2 s = wake
void screenBegin();
void screenLoop();
bool screenAsleep();
void screenNoteActivity();                  // input the UI acted on
void screenSleep(const char *reason);
bool screenTryWake(const char *reason);    // false while the post-sleep guard runs
void screenApplyBrightness();               // g_config.brightness, unless asleep

// One debounced wake button. Feed it the button's level every poll.
struct WakeButton {
    bool raw = false, stable = false, claimed = false;
    uint32_t changedMs = 0, downMs = 0;
};
// Applies the tap/hold rule. Returns true while the press belongs to the
// screen (pressed, or acted on this poll), so the caller must not also use it.
bool screenServiceWakeButton(WakeButton &b, bool pressed, uint32_t nowMs, const char *name);

// Screen timeout choices offered by the device and web settings (0 = never).
extern const uint32_t kScreenTimeoutSecs[];
extern const int kScreenTimeoutCount;
const char *screenTimeoutLabel(uint32_t secs);

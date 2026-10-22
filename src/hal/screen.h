#pragma once
#include <stdint.h>

// Screen power: the idle timeout, screensaver, sleep/wake, and the board wake
// buttons. Behaviour follows camillia-mt (without its lock screen):
//   - after g_config.screenOnSecs with no input the panel dims for its last
//     10 s, then shows the screensaver (g_config.screenSaver) or sleeps
//     (panel SLPIN, backlight off); 0 = never
//   - input that arrives while locked (screensaver or off) never reaches the
//     UI; only each board's wake gesture unlocks, and not in the first 3 s
//     after it locked (so the press that locked it cannot unlock it again)
//   - a board's wake button: tap = screen off when on or in the screensaver,
//     screensaver when off; hold 2 s = unlock
//   - a screensaver brought up by a tap goes back off after the timeout
//   - touch screens: a double tap on the screensaver unlocks; on a board with
//     no wake button (HAS_WAKE_BUTTON 0) it unlocks a dark screen too
//     (display.cpp)
enum class ScreenState : uint8_t { On, Saver, Off };

void screenBegin();
void screenLoop();
ScreenState screenState();
bool screenAsleep();                        // locked: screensaver or off
// The UI draws the screensaver; these run as it starts and ends.
void screenSetSaverHooks(void (*show)(), void (*hide)());
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

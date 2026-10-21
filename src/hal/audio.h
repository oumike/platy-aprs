#pragma once
#include <stddef.h>
#include <stdint.h>

// Tone output: the M9's piezo, the T-Deck's I2S amplifier or the Wio Tracker
// L2's ES8311 codec. Playback never blocks the caller; a new sequence replaces
// one still playing. Boards without any of these (Mesh Deck) stay silent.
struct Tone {
    uint16_t hz;   // 0 = rest
    uint16_t ms;
};

bool audioPresent();     // the board can make a sound at all
bool audioHasVolume();   // a speaker with amplitude control (not a piezo)
void audioBegin();
void audioLoop();        // call from loop()
// volumePct: 0..100, 50 = the board's normal loudness. Ignored by the piezo.
void audioPlay(const Tone *seq, size_t n, uint8_t volumePct);

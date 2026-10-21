#include "audio.h"
#include "board_pins.h"
#include <Arduino.h>

#if HAS_I2S_AUDIO
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#endif
#if HAS_ES8311
#include <AudioBoard.h>
#include <Wire.h>
#include "wio_l2_io.h"
#endif

bool audioPresent() { return HAS_BUZZER || HAS_I2S_AUDIO; }
bool audioHasVolume() { return HAS_I2S_AUDIO; }

#if HAS_BUZZER
// Piezo: one note at a time from audioLoop(), so nothing blocks.
namespace {
constexpr size_t kMaxTones = 12;
Tone s_seq[kMaxTones];
size_t s_len = 0, s_pos = 0;
uint32_t s_noteEndMs = 0;
bool s_playing = false;
}  // namespace

void audioBegin() {}

void audioPlay(const Tone *seq, size_t n, uint8_t) {
    s_len = n < kMaxTones ? n : kMaxTones;
    memcpy(s_seq, seq, s_len * sizeof(Tone));
    s_pos = 0;
    s_playing = s_len > 0;
    s_noteEndMs = millis();
    audioLoop();
}

void audioLoop() {
    if (!s_playing || (int32_t)(millis() - s_noteEndMs) < 0) return;
    if (s_pos >= s_len) {
        noTone(PIN_BUZZER);
        s_playing = false;
        return;
    }
    const Tone &t = s_seq[s_pos++];
    if (t.hz) tone(PIN_BUZZER, t.hz);
    else noTone(PIN_BUZZER);
    s_noteEndMs = millis() + t.ms;
}

#elif HAS_I2S_AUDIO
// Speaker: sine tones synthesized on a task of their own, since writing the
// samples (and, on the Wio, waking the amplifier) takes a few hundred ms.
// Ported from camillia-mt's pagerAudio*/tdeckAudio* players.
namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr uint32_t kRate = 44100;
constexpr int kChunk = 120;   // frames per i2s_write
constexpr size_t kMaxTones = 12;

struct Job {
    Tone seq[kMaxTones];
    uint8_t n;
    uint8_t volumePct;
};
QueueHandle_t s_q = nullptr;
bool s_ready = false;
uint8_t s_tries = 0;

#if HAS_ES8311
constexpr uint8_t kCodecVolActive = 50, kCodecVolIdle = 10;
audio_driver::DriverPins s_pins;
audio_driver::AudioBoard s_codec(audio_driver::AudioDriverES8311, s_pins);

bool codecBegin() {
    static bool pinsSet = false;
    if (!pinsSet) {
        s_pins.addI2C(audio_driver::PinFunction::CODEC, Wire);
        s_pins.addI2S(audio_driver::PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT, -1);
        (void)s_codec.driver().setI2CAddress(AUDIO_CODEC_ADDR);
        pinsSet = true;
    }
    audio_driver::CodecConfig cfg;
    cfg.input_device = audio_driver::ADC_INPUT_NONE;
    cfg.output_device = audio_driver::DAC_OUTPUT_ALL;
    cfg.i2s.bits = audio_driver::BIT_LENGTH_16BITS;
    cfg.i2s.rate = audio_driver::RATE_44K;
    cfg.i2s.channels = audio_driver::CHANNELS2;
    cfg.i2s.fmt = audio_driver::I2S_NORMAL;
    cfg.i2s.mode = audio_driver::MODE_SLAVE;
    if (!s_codec.begin(cfg)) return false;
    s_codec.setVolume(kCodecVolIdle);
    // Muted and unpowered between sounds: an idle DAC and amp click.
    s_codec.setMute(true);
    wioIoSet(EXP_AUDIO_PA, false);
    return true;
}
#endif

bool i2sBegin() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = kRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    esp_err_t err = i2s_driver_install(kPort, &cfg, 0, nullptr);
    if (err == ESP_ERR_INVALID_STATE) {
        i2s_driver_uninstall(kPort);
        err = i2s_driver_install(kPort, &cfg, 0, nullptr);
    }
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s install failed %d\n", (int)err);
        return false;
    }
    i2s_pin_config_t pins = {};
#if HAS_ES8311
    pins.mck_io_num = PIN_I2S_MCLK;
#else
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
    pins.bck_io_num = PIN_I2S_BCK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(kPort, &pins) != ESP_OK ||
        i2s_set_clk(kPort, kRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
        Serial.println("[audio] i2s setup failed");
        i2s_driver_uninstall(kPort);
        return false;
    }
    i2s_zero_dma_buffer(kPort);
    return true;
}

// A few attempts, then give up rather than retrying on every alert.
bool ensureReady() {
    if (s_ready) return true;
    if (s_tries >= 3) return false;
    s_tries++;
#if HAS_ES8311
    if (!codecBegin()) {
        Serial.println("[audio] ES8311 init failed");
        return false;
    }
#endif
    s_ready = i2sBegin();
    if (s_ready) Serial.println("[audio] ready");
    return s_ready;
}

void writeFrames(const int16_t *pcm, int frames) {
    size_t written = 0;
    i2s_write(kPort, pcm, (size_t)frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
}

void playSilence(uint16_t ms) {
    static const int16_t zero[kChunk * 2] = {};
    for (uint32_t left = (uint32_t)ms * kRate / 1000; left;) {
        const int n = left > (uint32_t)kChunk ? kChunk : (int)left;
        writeFrames(zero, n);
        left -= n;
    }
}

// A sine with short attack/release ramps, so notes start and stop without pops.
void playTone(uint16_t hz, uint16_t ms, float gain) {
    const uint32_t total = (uint32_t)ms * kRate / 1000;
    if (!total) return;
    uint32_t ramp = kRate / 400;
    if (ramp > total / 5) ramp = total / 5;
    if (ramp < 8) ramp = 8;
    const float step = 2.0f * (float)M_PI * hz / kRate;
    float phase = 0;
    int16_t pcm[kChunk * 2];
    for (uint32_t i = 0; i < total;) {
        const int n = total - i > (uint32_t)kChunk ? kChunk : (int)(total - i);
        for (int k = 0; k < n; k++, i++) {
            float env = 1.0f;
            if (i < ramp) env = (float)i / ramp;
            if (total - i < ramp) env = fminf(env, (float)(total - i) / ramp);
            const int16_t v = (int16_t)(sinf(phase) * gain * env);
            pcm[k * 2] = pcm[k * 2 + 1] = v;
            phase += step;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        writeFrames(pcm, n);
    }
}

void startPlayback() {
#if HAS_ES8311
    wioIoSet(EXP_AUDIO_PA, true);
    delay(AUDIO_AMP_SETTLE_MS);
    s_codec.setVolume(kCodecVolActive);   // raised while still muted: inaudible
#endif
    i2s_zero_dma_buffer(kPort);
    playSilence(6);   // prime the DMA so the first note isn't clipped
#if HAS_ES8311
    s_codec.setMute(false);
#endif
}

void stopPlayback() {
    playSilence(20);
#if HAS_ES8311
    s_codec.setMute(true);
    i2s_zero_dma_buffer(kPort);
    s_codec.setVolume(kCodecVolIdle);
    wioIoSet(EXP_AUDIO_PA, false);
#else
    i2s_zero_dma_buffer(kPort);
#endif
}

void audioTask(void *) {
    Job job;
    for (;;) {
        if (xQueueReceive(s_q, &job, portMAX_DELAY) != pdTRUE) continue;
        if (!job.volumePct || !ensureReady()) continue;
        // 50% is the board's established loudness; 100% doubles it (still
        // well inside int16 for both amplitudes).
        const float gain = AUDIO_TONE_AMPLITUDE * job.volumePct / 50.0f;
        startPlayback();
        for (uint8_t i = 0; i < job.n; i++) {
            if (job.seq[i].hz) playTone(job.seq[i].hz, job.seq[i].ms, gain);
            else playSilence(job.seq[i].ms);
        }
        stopPlayback();
    }
}
}  // namespace

void audioBegin() {
    if (s_q) return;
    s_q = xQueueCreate(1, sizeof(Job));
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 1, nullptr, 0);
}

void audioPlay(const Tone *seq, size_t n, uint8_t volumePct) {
    if (!s_q) return;
    Job job;
    job.n = (uint8_t)(n < kMaxTones ? n : kMaxTones);
    memcpy(job.seq, seq, job.n * sizeof(Tone));
    job.volumePct = volumePct > 100 ? 100 : volumePct;
    xQueueOverwrite(s_q, &job);   // newest wins if one is still waiting
}

void audioLoop() {}

#else
void audioBegin() {}
void audioLoop() {}
void audioPlay(const Tone *, size_t, uint8_t) {}
#endif

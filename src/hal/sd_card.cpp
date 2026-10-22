#include "sd_card.h"
#include "board_pins.h"
#if !HAS_SD_CARD
#include <LittleFS.h>

// No card slot: report "no card" everywhere. sdFs() still needs an object to
// return; callers check sdMounted() first, so it is never used.
bool sdBegin() {
    Serial.println("[sd] board has no card slot");
    return false;
}
void sdLoop() {}
bool sdHasSlot() { return false; }
bool sdMounted() { return false; }
bool sdRemount() { return false; }
uint32_t sdMountCount() { return 0; }
uint64_t sdTotalBytes() { return 0; }
uint64_t sdUsedBytes() { return 0; }
String sdStatusText() { return "no slot"; }
fs::FS &sdFs() { return LittleFS; }
bool sdMakeDirs(const char *) { return false; }
#else
#if SD_USE_SOFT_SPI
#include "sd_soft_spi.h"
#elif SD_USE_MMC
#include <SD_MMC.h>
#define CARD SD_MMC
#include "wio_l2_io.h"
#else
#include <SD.h>
#include <SPI.h>
#define CARD SD
#endif

namespace {

bool s_mounted = false;
uint32_t s_mountCount = 0;
uint8_t s_failStreak = 0;
uint32_t s_retryAtMs = 0;
uint32_t s_lastAttemptMs = 0;
constexpr uint32_t kBackoffMs[] = {2000, 5000, 15000, 30000, 60000};
constexpr uint32_t kMinAttemptGapMs = 3000;   // a failed probe can block for seconds

#if SD_USE_SOFT_SPI
// Bit-banged on pins of its own (both SPI hosts are taken), so nothing else
// needs deselecting.
void cardEnd() { sdSoftEnd(); }
bool cardPresent() { return sdSoftMounted(); }
uint64_t cardTotal() { return sdSoftCardSize(); }
uint64_t cardUsed() { return sdSoftUsedBytes(); }
#else
void cardEnd() { CARD.end(); }
bool cardPresent() { return CARD.cardType() != CARD_NONE; }
uint64_t cardTotal() { return CARD.totalBytes(); }
uint64_t cardUsed() { return CARD.usedBytes(); }
#endif

bool tryMount() {
#if SD_USE_SOFT_SPI
    if (!sdSoftBegin()) return false;
    Serial.printf("[sd] mounted (soft SPI, %s), %llu MB\n", sdSoftCardTypeName(),
                  (unsigned long long)(sdSoftCardSize() / (1024ULL * 1024ULL)));
    return true;
#elif SD_USE_MMC
    // As camillia-mt: power the slot (expander bit 14) and set the 1-bit pins on
    // every attempt, and leave it unpowered when there is nothing to mount. A
    // short power cycle first, so a card left half-initialised by an earlier
    // attempt starts clean.
    CARD.end();
    wioIoSet(EXP_SD_POWER, false);
    delay(20);
    if (!wioIoSet(EXP_SD_POWER, true)) {
        Serial.println("[sd] SD power enable failed (expander)");
        return false;
    }
    delay(20);
    if (!CARD.setPins(PIN_SDMMC_CLK, PIN_SDMMC_CMD, PIN_SDMMC_D0)) {
        Serial.printf("[sd] SDMMC pin setup failed clk=%d cmd=%d d0=%d\n", PIN_SDMMC_CLK, PIN_SDMMC_CMD,
                      PIN_SDMMC_D0);
        wioIoSet(EXP_SD_POWER, false);
        return false;
    }
    if (CARD.begin("/sdcard", /*mode1bit=*/true, /*format_if_mount_failed=*/false)) {
        if (CARD.cardType() != CARD_NONE) {
            Serial.printf("[sd] mounted (SDMMC 1-bit), %llu MB, %llu MB used\n",
                          (unsigned long long)(CARD.cardSize() / (1024ULL * 1024ULL)),
                          (unsigned long long)(CARD.usedBytes() / (1024ULL * 1024ULL)));
            return true;
        }
        CARD.end();
    }
    wioIoSet(EXP_SD_POWER, false);
    return false;
#else
    // The card shares the bus with the display and radio: keep their chip
    // selects up while it is probed. The SD spec initialises cards at <=400 kHz;
    // some refuse anything faster. Two rates, not more: with no card each one
    // blocks the loop for a moment, and this runs again in the background.
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    static const uint32_t kSpeeds[] = {4000000UL, 400000UL};
    for (uint32_t hz : kSpeeds) {
        if (SD.begin(PIN_SD_CS, SPI, hz, "/sdcard", 5)) {
            if (SD.cardType() != CARD_NONE) {
                Serial.printf("[sd] mounted at %lu Hz, %llu MB\n", (unsigned long)hz,
                              (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
                return true;
            }
        }
        CARD.end();
    }
    return false;
#endif
}

// One mount attempt, with the backoff bookkeeping. Logs only the first failure
// of a streak, so a board without a card says so once, not every minute.
bool attempt() {
    s_lastAttemptMs = millis();
    if (tryMount()) {
        s_mounted = true;
        s_mountCount++;
        s_failStreak = 0;
        return true;
    }
    if (s_failStreak == 0) Serial.println("[sd] no card mounted; retrying in the background");
    const size_t n = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);
    s_retryAtMs = millis() + kBackoffMs[s_failStreak < n ? s_failStreak : n - 1];
    if (s_failStreak < 0xFF) s_failStreak++;
    return false;
}

}  // namespace

bool sdBegin() {
#if !SD_USE_MMC && !SD_USE_SOFT_SPI
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
#endif
    return attempt();
}

void sdLoop() {
    if (s_mounted) return;
    if ((int32_t)(millis() - s_retryAtMs) < 0) return;
    attempt();
}

bool sdHasSlot() { return true; }
bool sdMounted() { return s_mounted; }
uint32_t sdMountCount() { return s_mountCount; }

bool sdRemount() {
    if (s_mounted && cardPresent()) return true;
    if (s_mounted) {
        // The card was pulled.
        Serial.println("[sd] card gone");
        s_mounted = false;
        cardEnd();
    }
    if (s_lastAttemptMs && millis() - s_lastAttemptMs < kMinAttemptGapMs) return false;
    return attempt();
}

uint64_t sdTotalBytes() { return s_mounted ? cardTotal() : 0; }
uint64_t sdUsedBytes() { return s_mounted ? cardUsed() : 0; }

String sdStatusText() {
    if (s_mounted) {
        const uint64_t total = cardTotal(), used = cardUsed();
        char buf[40];
        if (total >= 1024ULL * 1024ULL * 1024ULL)
            snprintf(buf, sizeof(buf), "%.1f GB card", total / (1024.0 * 1024.0 * 1024.0));
        else snprintf(buf, sizeof(buf), "%u MB card", (unsigned)(total / (1024 * 1024)));
        String s = buf;
        if (total) s += ", " + String((unsigned)(used * 100 / total)) + "% used";
        return s;
    }
    const int32_t left = (int32_t)(s_retryAtMs - millis());
    return left > 0 ? "no card (retry in " + String((left + 999) / 1000) + " s)" : String("no card");
}

#if SD_USE_SOFT_SPI
fs::FS &sdFs() { return sdSoftFs(); }
#else
fs::FS &sdFs() { return CARD; }
#endif

bool sdMakeDirs(const char *path) {
    if (!s_mounted) return false;
    String p;
    const String full(path);
    int start = 1;
    while (true) {
        const int slash = full.indexOf('/', start);
        p = slash < 0 ? full : full.substring(0, slash);
        if (!sdFs().exists(p) && !sdFs().mkdir(p)) return false;
        if (slash < 0) return true;
        start = slash + 1;
    }
}
#endif  // HAS_SD_CARD

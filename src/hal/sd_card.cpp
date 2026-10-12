#include "sd_card.h"
#include <SD.h>
#include <SPI.h>
#include "pins_m9.h"

static bool s_mounted;

static bool tryMount() {
    // The SD spec initialises cards at <=400 kHz; some refuse anything faster.
    static const uint32_t kSpeeds[] = {4000000UL, 1000000UL, 400000UL};
    for (uint32_t hz : kSpeeds) {
        if (SD.begin(PIN_SD_CS, SPI, hz, "/sd", 5)) {
            Serial.printf("[sd] mounted at %lu Hz, %llu MB\n", (unsigned long)hz,
                          (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
            return true;
        }
        SD.end();
    }
    Serial.println("[sd] no card");
    return false;
}

bool sdBegin() {
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    s_mounted = tryMount();
    return s_mounted;
}

bool sdMounted() { return s_mounted; }

bool sdRemount() {
    if (s_mounted && SD.cardType() != CARD_NONE) return true;
    // A failed probe blocks for seconds; don't repeat it on every request.
    static uint32_t lastFailMs;
    if (lastFailMs && millis() - lastFailMs < 10000) return false;
    SD.end();
    s_mounted = tryMount();
    lastFailMs = s_mounted ? 0 : millis();
    return s_mounted;
}

uint64_t sdTotalBytes() { return s_mounted ? SD.totalBytes() : 0; }
uint64_t sdUsedBytes() { return s_mounted ? SD.usedBytes() : 0; }
fs::FS &sdFs() { return SD; }

bool sdMakeDirs(const char *path) {
    if (!s_mounted) return false;
    String p;
    const String full(path);
    int start = 1;
    while (true) {
        const int slash = full.indexOf('/', start);
        p = slash < 0 ? full : full.substring(0, slash);
        if (!SD.exists(p) && !SD.mkdir(p)) return false;
        if (slash < 0) return true;
        start = slash + 1;
    }
}

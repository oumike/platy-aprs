#include "sd_card.h"
#include "board_pins.h"
#if SD_USE_MMC
#include <SD_MMC.h>
#define CARD SD_MMC
#include "wio_l2_io.h"
#else
#include <SD.h>
#include <SPI.h>
#define CARD SD
#endif

static bool s_mounted;

// Data written before the project rename lives under /rhino.
static void migrateLegacyDir() {
    if (CARD.exists("/rhino") && !CARD.exists("/platy")) {
        bool ok = CARD.rename("/rhino", "/platy");
        Serial.printf("[sd] migrate /rhino -> /platy: %s\n", ok ? "ok" : "failed");
    }
}

static bool tryMount() {
#if SD_USE_MMC
    if (CARD.begin("/sd", true, false)) {
        if (CARD.cardType() != CARD_NONE) {
            Serial.printf("[sd] mounted (SDMMC), %llu MB\n",
                          (unsigned long long)(CARD.cardSize() / (1024ULL * 1024ULL)));
            return true;
        }
        CARD.end();
    }
    Serial.println("[sd] no card");
    return false;
#else
    // The SD spec initialises cards at <=400 kHz; some refuse anything faster.
    static const uint32_t kSpeeds[] = {4000000UL, 1000000UL, 400000UL};
    for (uint32_t hz : kSpeeds) {
        if (SD.begin(PIN_SD_CS, SPI, hz, "/sd", 5)) {
            Serial.printf("[sd] mounted at %lu Hz, %llu MB\n", (unsigned long)hz,
                          (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
            return true;
        }
        CARD.end();
    }
    Serial.println("[sd] no card");
    return false;
#endif
}

bool sdBegin() {
#if SD_USE_MMC
    wioIoSet(EXP_SD_POWER, true);
    delay(10);
    CARD.setPins(PIN_SDMMC_CLK, PIN_SDMMC_CMD, PIN_SDMMC_D0);
#else
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
#endif
    s_mounted = tryMount();
    if (s_mounted) migrateLegacyDir();
    return s_mounted;
}

bool sdMounted() { return s_mounted; }

bool sdRemount() {
    if (s_mounted && CARD.cardType() != CARD_NONE) return true;
    // A failed probe blocks for seconds; don't repeat it on every request.
    static uint32_t lastFailMs;
    if (lastFailMs && millis() - lastFailMs < 10000) return false;
    CARD.end();
    s_mounted = tryMount();
    lastFailMs = s_mounted ? 0 : millis();
    return s_mounted;
}

uint64_t sdTotalBytes() { return s_mounted ? CARD.totalBytes() : 0; }
uint64_t sdUsedBytes() { return s_mounted ? CARD.usedBytes() : 0; }
fs::FS &sdFs() { return CARD; }

bool sdMakeDirs(const char *path) {
    if (!s_mounted) return false;
    String p;
    const String full(path);
    int start = 1;
    while (true) {
        const int slash = full.indexOf('/', start);
        p = slash < 0 ? full : full.substring(0, slash);
        if (!CARD.exists(p) && !CARD.mkdir(p)) return false;
        if (slash < 0) return true;
        start = slash + 1;
    }
}

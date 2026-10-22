#include "ota.h"
#include "config/config.h"
#include "hal/board_pins.h"
#include "ota_signing_pubkey.h"
#include "version.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

namespace {

// Hardcoded on purpose, as in camillia-mt: the update source cannot be
// repointed from the UI or config. Plain http:// because authenticity comes
// from the signature, not the transport.
constexpr const char *kBaseUrl = "http://ota.platy.sumat.org/firmware";
constexpr uint32_t kTimeoutMs = 12000;
constexpr uint32_t kFirstCheckDelayMs = 20000;           // after joining Wi-Fi
constexpr uint32_t kCheckIntervalMs = 24UL * 3600UL * 1000UL;

SemaphoreHandle_t s_mtx = nullptr;
OtaStatus s_st;
volatile bool s_busy = false;
bool s_wasConnected = false;
bool s_autoTried = false;   // one automatic install per release found
uint32_t s_nextCheckMs = 0;
uint32_t s_rebootAtMs = 0;

template <class F> void locked(F f) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    f();
    xSemaphoreGive(s_mtx);
}

void setState(OtaState st, const String &err = String()) {
    locked([&] {
        s_st.state = st;
        s_st.error = err;
    });
}

// Numeric components before any '-' or '+' ("v0.2.10+abc" -> 0,2,10). Build
// metadata never makes a build look newer, so a dist-only build of a release
// is not offered that same release.
int compareVersions(const char *a, const char *b) {
    auto next = [](const char *&p) -> long {
        while (*p && *p != '-' && *p != '+' && !isdigit((unsigned char)*p)) p++;
        if (!*p || *p == '-' || *p == '+') return -1;
        long v = 0;
        while (isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
        return v;
    };
    while (true) {
        long va = next(a), vb = next(b);
        if (va < 0 && vb < 0) return 0;
        if (va < 0) va = 0;
        if (vb < 0) vb = 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
}

void beginRequest(HTTPClient &http, WiFiClient &client, const String &url) {
    http.begin(client, url);
    http.setTimeout((uint16_t)kTimeoutMs);
    http.addHeader("User-Agent", APP_ID "-ota/" APP_VERSION);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
}

// Streams the release JSON and stops at "tag_name", which GitHub puts well
// ahead of the (large, growing) assets array. HTTP/1.0 so the body can never
// be chunked: the scan reads the raw socket.
bool fetchLatestTag(String &tag, String &err) {
    WiFiClient client;
    HTTPClient http;
    beginRequest(http, client, String(kBaseUrl) + "/latest");
    http.addHeader("Accept", "application/vnd.github+json");
    http.useHTTP10(true);
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = code <= 0 ? "Network error (" + String(code) + ")" : "HTTP " + String(code);
        http.end();
        return false;
    }
    WiFiClient *s = http.getStreamPtr();
    static const char kKey[] = "\"tag_name\":\"";
    String window;
    size_t scanned = 0;
    uint32_t lastMs = millis();
    while (s && http.connected() && scanned < 16384 && !tag.length()) {
        const size_t avail = s->available();
        if (!avail) {
            if (millis() - lastMs > kTimeoutMs) break;
            delay(5);
            continue;
        }
        char buf[256];
        const int n = s->readBytes((uint8_t *)buf, min(avail, sizeof(buf)));
        if (n <= 0) continue;
        lastMs = millis();
        scanned += n;
        window.concat(buf, n);
        const int k = window.indexOf(kKey);
        if (k >= 0) {
            const int start = k + (int)strlen(kKey), end = window.indexOf('"', start);
            if (end >= 0) tag = window.substring(start, end);
            continue;
        }
        if (window.length() > 96) window.remove(0, window.length() - 96);
    }
    http.end();
    if (tag.length() && tag.length() < 40) return true;
    err = scanned ? "Release tag not found" : "No release response";
    tag = "";
    return false;
}

String assetUrl(const String &tag) {
    return String(kBaseUrl) + "/" + tag + "/platy-aprs-" + otaAssetSlug() + "-" + tag + "-ota.bin";
}

bool fetchSmall(const String &url, uint8_t *buf, size_t cap, size_t &len, String &err) {
    WiFiClient client;
    HTTPClient http;
    beginRequest(http, client, url);
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = "signature HTTP " + String(code);
        http.end();
        return false;
    }
    const int total = http.getSize();
    if (total > (int)cap) {
        err = "signature too large";
        http.end();
        return false;
    }
    WiFiClient *s = http.getStreamPtr();
    len = 0;
    uint32_t lastMs = millis();
    while (s && http.connected() && (total < 0 || len < (size_t)total) && len < cap) {
        const size_t avail = s->available();
        if (!avail) {
            if (millis() - lastMs > 5000) break;
            delay(5);
            continue;
        }
        const int n = s->readBytes(buf + len, min(avail, cap - len));
        if (n > 0) {
            len += n;
            lastMs = millis();
        }
    }
    http.end();
    if (!len) err = "empty signature";
    return len > 0;
}

// Detached ECDSA P-256 (DER) over the SHA-256 of the image. Fails closed.
bool verifySignature(const uint8_t *hash, const uint8_t *sig, size_t sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)OTA_SIGNING_PUBKEY_PEM,
                                         strlen(OTA_SIGNING_PUBKEY_PEM) + 1);
    if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    if (rc != 0) Serial.printf("[ota] signature check failed: -0x%04x\n", (unsigned)-rc);
    return rc == 0;
}

// The dual-slot table this project ships: ota_0 at 0x10000, an ota_1 of the
// same size, no ota_2, and the running app in one of them. Anything else (a
// third-party installer's layout) gets no update offered: the spare slot may
// hold another firmware.
bool layoutSupportsUpdate() {
    const esp_partition_t *s0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *s1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t *s2 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, nullptr);
    if (!s0 || !s1 || s2 || s0->size != s1->size || s0->address != 0x10000) return false;
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run && (run->address == s0->address || run->address == s1->address);
}

void doCheck() {
    setState(OtaState::Checking);
    String tag, err;
    if (!fetchLatestTag(tag, err)) {
        Serial.printf("[ota] check failed: %s\n", err.c_str());
        setState(OtaState::Failed, err);
        return;
    }
    const bool newer = compareVersions(APP_VERSION, tag.c_str()) < 0;
    locked([&] {
        if (s_st.latest != tag) s_autoTried = false;
        s_st.latest = tag;
    });
    Serial.printf("[ota] latest %s, running %s%s\n", tag.c_str(), APP_VERSION, newer ? ": update available" : "");
    setState(newer ? OtaState::Available : OtaState::UpToDate);
}

bool doInstall(String &err) {
    String tag;
    locked([&] { tag = s_st.latest; });
    if (!tag.length() && !fetchLatestTag(tag, err)) return false;
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (!next) {
        err = "No OTA app partition";
        return false;
    }
    const String url = assetUrl(tag);

    // Signature first: a missing one aborts before the slot is touched.
    uint8_t sig[96];
    size_t sigLen = 0;
    if (!fetchSmall(url + ".sig", sig, sizeof(sig), sigLen, err)) return false;

    WiFiClient client;
    HTTPClient http;
    beginRequest(http, client, url);
    http.setTimeout(30000);
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = code <= 0 ? "Download failed (" + String(code) + ")" : "Download HTTP " + String(code);
        http.end();
        return false;
    }
    const int len = http.getSize();
    if (len > 0 && (size_t)len > next->size) {
        err = "Image larger than the app slot: install over USB";
        http.end();
        return false;
    }
    if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
        err = "Update.begin failed (" + String(Update.getError()) + ")";
        http.end();
        return false;
    }
    locked([&] {
        s_st.written = 0;
        s_st.total = len > 0 ? len : 0;
    });

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    WiFiClient &s = http.getStream();
    uint8_t chunk[1024];
    size_t written = 0;
    uint32_t lastMs = millis();
    bool ok = true;
    while (http.connected() && (len <= 0 || written < (size_t)len)) {
        const size_t avail = s.available();
        const int n = avail ? s.readBytes(chunk, min(avail, sizeof(chunk))) : 0;
        if (n <= 0) {
            if (millis() - lastMs > 15000) {
                err = "Download stalled";
                ok = false;
                break;
            }
            delay(10);
            continue;
        }
        if (Update.write(chunk, n) != (size_t)n) {
            err = "Flash write failed";
            ok = false;
            break;
        }
        mbedtls_sha256_update(&sha, chunk, n);
        written += n;
        lastMs = millis();
        locked([&] { s_st.written = written; });
    }
    http.end();
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (ok && len > 0 && written != (size_t)len) {
        err = "Download incomplete";
        ok = false;
    }
    // Verified BEFORE the slot is made bootable: a tampered or corrupt image
    // fails here and is never booted.
    if (ok && !verifySignature(digest, sig, sigLen)) {
        err = "Signature verification failed";
        ok = false;
    }
    if (!ok) {
        Update.abort();
        return false;
    }
    if (!Update.end() || !Update.isFinished()) {
        err = "Update.end failed (" + String(Update.getError()) + ")";
        return false;
    }
    Serial.printf("[ota] installed %s (%u bytes)\n", tag.c_str(), (unsigned)written);
    return true;
}

void worker(void *arg) {
    const bool install = arg != nullptr;
    if (install) {
        setState(OtaState::Installing);
        String err;
        if (doInstall(err)) {
            setState(OtaState::Done);
            s_rebootAtMs = millis() + 2000;
        } else {
            Serial.printf("[ota] install failed: %s\n", err.c_str());
            setState(OtaState::Failed, err);
        }
    } else {
        doCheck();
    }
    s_busy = false;
    vTaskDelete(nullptr);
}

bool start(bool install) {
    if (s_busy || !s_st.layoutOk || !otaAssetSlug()[0] || WiFi.status() != WL_CONNECTED) return false;
    if (s_st.state == OtaState::Done) return false;
    s_busy = true;
    if (xTaskCreatePinnedToCore(worker, install ? "ota-inst" : "ota-chk", 12288,
                                install ? (void *)1 : nullptr, 1, nullptr, 0) != pdPASS) {
        s_busy = false;
        return false;
    }
    return true;
}

}  // namespace

const char *otaAssetSlug() {
#if defined(DEVICE_M9)
    return "m9";
#elif defined(DEVICE_WIO_TRACKER_L2)
    return "wio-tracker-l2";
#elif defined(DEVICE_TDECK)
    return "tdeck-plus";
#elif defined(DEVICE_MESH_DECK)
    return "mesh-deck";
#elif defined(DEVICE_CROWPANEL_35)
    return "crowpanel-35";
#else
    return "";
#endif
}

const char *otaStateName(OtaState s) {
    switch (s) {
        case OtaState::Idle: return "idle";
        case OtaState::Checking: return "checking";
        case OtaState::UpToDate: return "up-to-date";
        case OtaState::Available: return "available";
        case OtaState::Installing: return "installing";
        case OtaState::Done: return "done";
        case OtaState::Failed: return "failed";
    }
    return "?";
}

void otaBegin() {
    s_mtx = xSemaphoreCreateMutex();
    s_st.layoutOk = layoutSupportsUpdate();
    if (!s_st.layoutOk) Serial.println("[ota] flash layout not ours: updates disabled");
}

OtaStatus otaStatus() {
    OtaStatus st;
    locked([&] { st = s_st; });
    return st;
}

bool otaStartCheck() { return start(false); }
bool otaStartInstall() { return start(true); }

void otaLoop() {
    const uint32_t now = millis();
    if (s_rebootAtMs && (int32_t)(now - s_rebootAtMs) >= 0) ESP.restart();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !s_wasConnected) s_nextCheckMs = now + kFirstCheckDelayMs;
    s_wasConnected = connected;
    if (!connected || s_busy) return;
    if (g_config.otaAutoInstall && !s_autoTried && otaStatus().state == OtaState::Available) {
        s_autoTried = true;
        Serial.println("[ota] installing automatically");
        otaStartInstall();
        return;
    }
    if (!s_nextCheckMs || (int32_t)(now - s_nextCheckMs) < 0) return;
    s_nextCheckMs = now + kCheckIntervalMs;
    otaStartCheck();
}

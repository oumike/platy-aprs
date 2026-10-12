#include "aprs_is.h"
#include <WiFi.h>
#include "config/config.h"
#include "net/wifi_manager.h"
#include "services/router.h"
#include "version.h"

namespace {

constexpr size_t kLineMax = 512;
constexpr uint32_t kSilentMs = 90000;   // servers send a comment every ~20s

struct Line {
    uint16_t len;
    char data[kLineMax];
};

struct Want {
    bool enabled = false;
    String host;
    uint16_t port = 0;
    String login;
    uint32_t gen = 0;
};

SemaphoreHandle_t s_mtx;
QueueHandle_t s_rxQ, s_txQ;
Want s_want;
AprsIsStatus s_st{"off"};
uint32_t s_connectedAtMs;
String s_wantKey;
uint32_t s_lastCheckMs;

template <class F> void locked(F f) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    f();
    xSemaphoreGive(s_mtx);
}

Want want() {
    Want w;
    locked([&] { w = s_want; });
    return w;
}

void setState(const char *state, bool connected, const String *err = nullptr) {
    locked([&] {
        s_st.state = state;
        if (connected && !s_st.connected) s_connectedAtMs = millis();
        s_st.connected = connected;
        if (!connected) s_st.verified = false;
        if (err) s_st.lastError = *err;
    });
}

// Sleeps up to ms, returning early if the wanted connection changed.
void waitOrChange(uint32_t ms, uint32_t gen) {
    for (uint32_t t = 0; t < ms && want().gen == gen; t += 250) vTaskDelay(pdMS_TO_TICKS(250));
}

void handleServerLine(const char *l) {
    if (l[0] == '#') {
        if (strstr(l, "logresp")) {
            const bool ok = strstr(l, " verified") != nullptr;
            String server;
            if (const char *s = strstr(l, "server ")) server = String(s + 7);
            server.trim();
            locked([&] {
                s_st.verified = ok;
                s_st.server = server;
                if (!ok) s_st.lastError = "login not verified (check callsign/passcode)";
            });
            Serial.printf("[is] %s\n", l);
        }
        return;
    }
    Line q;
    q.len = strnlen(l, kLineMax - 1);
    memcpy(q.data, l, q.len);
    q.data[q.len] = 0;
    xQueueSend(s_rxQ, &q, 0);   // dropped when the loop task is behind
    locked([] { s_st.rxLines++; });
}

void task(void *) {
    WiFiClient c;
    uint32_t backoffS = 5;
    static char buf[kLineMax];
    static uint8_t chunk[256];
    for (;;) {
        const Want w = want();
        if (!w.enabled) {
            setState(g_config.aprsIsEnabled ? "waiting for Wi-Fi" : "off", false);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        setState("connecting", false);
        Serial.printf("[is] connecting to %s:%u\n", w.host.c_str(), w.port);
        if (!c.connect(w.host.c_str(), w.port, 8000)) {
            const String err = "connect to " + w.host + " failed";
            setState("waiting to retry", false, &err);
            Serial.printf("[is] %s, retry in %lus\n", err.c_str(), (unsigned long)backoffS);
            waitOrChange(backoffS * 1000, w.gen);
            backoffS = min<uint32_t>(backoffS * 2, 120);
            continue;
        }
        c.setNoDelay(true);
        c.print(w.login);
        xQueueReset(s_txQ);
        const String noErr;
        setState("connected", true, &noErr);
        size_t n = 0;
        uint32_t lastRxMs = millis();
        String err;
        while (c.connected()) {
            if (want().gen != w.gen) break;
            int avail = c.available();
            while (avail > 0) {
                const int got = c.read(chunk, min<int>(avail, sizeof(chunk)));
                if (got <= 0) break;
                avail -= got;
                lastRxMs = millis();
                for (int i = 0; i < got; i++) {
                    const char ch = (char)chunk[i];
                    if (ch == '\n' || ch == '\r') {
                        if (n) {
                            buf[n] = 0;
                            handleServerLine(buf);
                            n = 0;
                        }
                    } else if (n < kLineMax - 1) {
                        buf[n++] = ch;
                    }
                }
            }
            Line out;
            while (xQueueReceive(s_txQ, &out, 0) == pdTRUE) {
                c.write((const uint8_t *)out.data, out.len);
                c.write((const uint8_t *)"\r\n", 2);
                locked([] { s_st.txLines++; });
            }
            if (millis() - lastRxMs > kSilentMs) {
                err = "server went silent";
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        c.stop();
        if (err.isEmpty() && want().gen == w.gen) err = "disconnected by server";
        Serial.printf("[is] disconnected: %s\n", err.length() ? err.c_str() : "config changed");
        setState("waiting to retry", false, err.length() ? &err : nullptr);
        if (err.length()) {
            waitOrChange(backoffS * 1000, w.gen);
            backoffS = min<uint32_t>(backoffS * 2, 120);
        } else {
            backoffS = 5;
        }
    }
}

bool callsignSet() { return g_config.callsign.length() && !g_config.callsign.equalsIgnoreCase("N0CALL"); }

}  // namespace

void aprsIsBegin() {
    s_mtx = xSemaphoreCreateMutex();
    s_rxQ = xQueueCreate(16, sizeof(Line));
    s_txQ = xQueueCreate(8, sizeof(Line));
    xTaskCreatePinnedToCore(task, "aprsis", 6144, nullptr, 1, nullptr, 0);
}

void aprsIsLoop() {
    if (!s_mtx) return;
    if (millis() - s_lastCheckMs >= 1000) {
        s_lastCheckMs = millis();
        Want w;
        w.enabled = g_config.aprsIsEnabled && callsignSet() && wifiStaConnected() && g_config.aprsIsServer.length();
        if (w.enabled) {
            w.host = g_config.aprsIsServer;
            w.port = g_config.aprsIsPort;
            String call = g_config.fullCallsign();
            call.toUpperCase();
            w.login = "user " + call + " pass " + String(g_config.effectivePasscode()) + " vers " APP_NAME " " APP_VERSION;
            if (g_config.aprsIsFilter.length()) w.login += " filter " + g_config.aprsIsFilter;
            w.login += "\r\n";
        }
        const String key = String(w.enabled) + w.host + ":" + w.port + w.login;
        if (key != s_wantKey) {
            s_wantKey = key;
            locked([&] {
                w.gen = s_want.gen + 1;
                s_want = w;
            });
        }
    }
    Line l;
    // Bounded so a burst cannot starve the UI.
    for (int i = 0; i < 4 && xQueueReceive(s_rxQ, &l, 0) == pdTRUE; i++) routerOnIsLine(std::string(l.data, l.len));
}

bool aprsIsReady() {
    if (!s_mtx) return false;
    bool r;
    locked([&] { r = s_st.connected && s_st.verified; });
    return r;
}

bool aprsIsSend(const std::string &line) {
    if (!aprsIsReady() || line.size() >= kLineMax) return false;
    Line l;
    l.len = line.size();
    memcpy(l.data, line.data(), l.len);
    return xQueueSend(s_txQ, &l, 0) == pdTRUE;
}

AprsIsStatus aprsIsStatus() {
    AprsIsStatus s{"off"};
    if (!s_mtx) return s;
    locked([&] {
        s = s_st;
        s.connectedForS = s.connected ? (millis() - s_connectedAtMs) / 1000 : 0;
    });
    return s;
}

void aprsIsCountGated(bool rfToIs) {
    if (!s_mtx) return;
    locked([&] { (rfToIs ? s_st.gatedRfToIs : s_st.gatedIsToRf)++; });
}

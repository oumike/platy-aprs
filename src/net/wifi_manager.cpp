#include "wifi_manager.h"
#include "config/config.h"
#include <ESPmDNS.h>
#include <esp_netif.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <algorithm>

namespace {

constexpr uint32_t kApFallbackAfterMs = 60UL * 1000;       // STA down this long -> AP up
constexpr uint32_t kApShutdownGraceMs = 15UL * 1000;       // let the browser see the new IP
constexpr uint32_t kStaRetryMs = 20UL * 1000;
constexpr uint32_t kStaRetryWithApClientsMs = 120UL * 1000;  // retries hop channels; spare AP clients
const IPAddress kApIp(192, 168, 4, 1);

bool s_apActive = false;
ApReason s_apReason = ApReason::None;
uint32_t s_apStartedMs = 0;
String s_apSsid;

bool s_staWasConnected = false;
uint32_t s_staDownSinceMs = 0;
uint32_t s_staConnectedSinceMs = 0;
uint32_t s_lastStaAttemptMs = 0;
bool s_scanHeldSta = false;   // a scan cancelled the connection attempt; retry once it ends
bool s_mdnsStarted = false;

bool haveCreds() { return g_config.wifiSsid.length() > 0; }

void startAp(ApReason reason) {
    s_apReason = reason;
    s_apStartedMs = millis();
    if (s_apActive) return;
    WiFi.mode(haveCreds() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
    WiFi.softAP(s_apSsid.c_str());   // open network by design
    s_apActive = true;
    Serial.printf("[wifi] AP '%s' up (%s)\n", s_apSsid.c_str(), apReasonStr(reason));
}

void stopAp() {
    if (!s_apActive) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_apActive = false;
    s_apReason = ApReason::None;
    Serial.println("[wifi] AP shut down");
}

void beginSta() {
    if (!haveCreds()) return;
    s_lastStaAttemptMs = millis();
    WiFi.setHostname(g_config.hostname.c_str());
    WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
    Serial.printf("[wifi] connecting to '%s'\n", g_config.wifiSsid.c_str());
}

void onStaUp() {
    s_staConnectedSinceMs = millis();
    Serial.printf("[wifi] connected to '%s', IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    if (!s_mdnsStarted && MDNS.begin(g_config.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        s_mdnsStarted = true;
    }
    configTzTime(g_config.timezone.c_str(), "pool.ntp.org", "time.nist.gov");
}

}  // namespace

const char *apReasonStr(ApReason r) {
    switch (r) {
        case ApReason::Setup: return "setup";
        case ApReason::Fallback: return "fallback";
        case ApReason::Manual: return "manual";
        default: return "none";
    }
}

void wifiBegin() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "PLATY-APRS-%02X%02X", mac[4], mac[5]);
    s_apSsid = buf;

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);   // retries are paced by wifiLoop()
    WiFi.setSleep(true);            // modem sleep; required alongside BLE

    if (!g_config.wifiEnabled) {
        // Arduino starts the TCP/IP stack only when Wi-Fi first comes on; the web
        // server's socket still needs it (lwIP asserts "Invalid mbox" otherwise).
        // Arduino's own init calls this again harmlessly once Wi-Fi is turned on.
        esp_netif_init();
        WiFi.mode(WIFI_OFF);
        Serial.println("[wifi] off");
    } else if (haveCreds()) {
        WiFi.mode(WIFI_STA);
        s_staDownSinceMs = millis();
        beginSta();
    } else {
        startAp(ApReason::Setup);
    }
}

void wifiLoop() {
    if (!g_config.wifiEnabled) return;
    const uint32_t now = millis();

    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !s_staWasConnected) onStaUp();
    if (!up && s_staWasConnected) {
        s_staDownSinceMs = now;
        Serial.println("[wifi] STA connection lost");
    }
    s_staWasConnected = up;

    if (up) {
        if (s_apActive && now - s_staConnectedSinceMs > kApShutdownGraceMs) stopAp();
        return;
    }

    if (!haveCreds()) {
        if (!s_apActive) startAp(ApReason::Setup);
        return;
    }

    if (!s_apActive && now - s_staDownSinceMs > kApFallbackAfterMs) startAp(ApReason::Fallback);

    // A connection attempt would abort a running scan.
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        s_lastStaAttemptMs = now;
        s_scanHeldSta = true;
        return;
    }
    const bool apClients = s_apActive && WiFi.softAPgetStationNum() > 0;
    const uint32_t retry = apClients ? kStaRetryWithApClientsMs : kStaRetryMs;
    if ((s_scanHeldSta && !apClients) || now - s_lastStaAttemptMs > retry) {
        s_scanHeldSta = false;
        WiFi.disconnect(false, false);
        beginSta();
    }
}

void wifiApplyConfig() {
    if (!g_config.wifiEnabled) return;   // new credentials wait until Wi-Fi is turned on
    WiFi.disconnect(false, false);
    s_staWasConnected = false;
    s_staDownSinceMs = millis();
    if (haveCreds()) {
        if (s_apActive) WiFi.mode(WIFI_AP_STA);
        beginSta();
    } else if (!s_apActive) {
        startAp(ApReason::Setup);
    }
}

bool wifiScanStart() {
    if (!g_config.wifiEnabled) return false;
    if (wifiScanRunning()) return true;
    if (!wifiStaConnected()) {
        // The driver refuses to scan while the station is connecting.
        WiFi.disconnect(false, false);
        s_lastStaAttemptMs = millis();
    }
    const int16_t r = WiFi.scanNetworks(true, false);
    if (r == WIFI_SCAN_FAILED) {
        Serial.println("[wifi] scan failed to start");
        return false;
    }
    return true;
}

bool wifiScanRunning() { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }

bool wifiScanResults(std::vector<WifiNetwork> &out) {
    out.clear();
    const int16_t n = WiFi.scanComplete();
    if (n < 0) return false;
    for (int i = 0; i < n; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        const int8_t rssi = (int8_t)WiFi.RSSI(i);
        auto it = std::find_if(out.begin(), out.end(), [&](const WifiNetwork &w) { return w.ssid == ssid; });
        if (it != out.end()) {
            if (rssi > it->rssi) it->rssi = rssi;
            continue;
        }
        out.push_back({ssid, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    std::stable_sort(out.begin(), out.end(), [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });
    return true;
}

bool wifiConnectTo(const String &ssid, const String &password, String &err) {
    AppConfig next = g_config;
    next.wifiSsid = ssid;
    if (password.length() || ssid != g_config.wifiSsid) next.wifiPassword = password;
    if (!ssid.length()) err = "Enter a network name";
    else err = configValidate(next);
    if (err.length()) return false;
    g_config = next;
    if (!configSave()) {
        err = "Failed to save settings";
        return false;
    }
    wifiApplyConfig();
    return true;
}

bool wifiOpenSetupAp() {
    if (!g_config.wifiEnabled || wifiStaConnected()) return false;   // the AP never runs alongside a working STA link
    startAp(ApReason::Manual);
    return true;
}

void wifiSetEnabled(bool on) {
    if (on == g_config.wifiEnabled) return;
    g_config.wifiEnabled = on;
    configSave();
    if (!on) {
        stopAp();
        if (s_mdnsStarted) {
            MDNS.end();
            s_mdnsStarted = false;
        }
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        s_staWasConnected = false;
        Serial.println("[wifi] turned off");
        return;
    }
    Serial.println("[wifi] turned on");
    s_staDownSinceMs = millis();
    if (haveCreds()) {
        WiFi.mode(WIFI_STA);
        beginSta();
    } else {
        startAp(ApReason::Setup);
    }
}

bool wifiStaConnected() { return WiFi.status() == WL_CONNECTED; }

WifiStatus wifiStatus() {
    WifiStatus s;
    s.enabled = g_config.wifiEnabled;
    s.apActive = s_apActive;
    s.apReason = s_apReason;
    s.apSsid = s_apSsid;
    s.apIp = s_apActive ? WiFi.softAPIP().toString() : String();
    s.apClients = s_apActive ? WiFi.softAPgetStationNum() : 0;
    s.staConfigured = haveCreds();
    s.staConnected = wifiStaConnected();
    s.staSsid = g_config.wifiSsid;
    s.staIp = s.staConnected ? WiFi.localIP().toString() : String();
    s.rssi = s.staConnected ? (int8_t)WiFi.RSSI() : 0;
    s.hostname = g_config.hostname;
    return s;
}

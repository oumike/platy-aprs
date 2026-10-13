#include "wifi_manager.h"
#include "config/config.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_sntp.h>

namespace {

constexpr uint32_t kApFallbackAfterMs = 60UL * 1000;       // STA down this long -> AP up
constexpr uint32_t kApShutdownGraceMs = 15UL * 1000;       // let the browser see the new IP
constexpr uint32_t kManualApMinMs = 10UL * 60 * 1000;      // manual AP lifetime when STA is up
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

    if (haveCreds()) {
        WiFi.mode(WIFI_STA);
        s_staDownSinceMs = millis();
        beginSta();
    } else {
        startAp(ApReason::Setup);
    }
}

void wifiLoop() {
    const uint32_t now = millis();

    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !s_staWasConnected) onStaUp();
    if (!up && s_staWasConnected) {
        s_staDownSinceMs = now;
        Serial.println("[wifi] STA connection lost");
    }
    s_staWasConnected = up;

    if (up) {
        if (s_apActive) {
            const bool manualExpired =
                s_apReason != ApReason::Manual || now - s_apStartedMs > kManualApMinMs;
            if (manualExpired && now - s_staConnectedSinceMs > kApShutdownGraceMs) stopAp();
        }
        return;
    }

    if (!haveCreds()) {
        if (!s_apActive) startAp(ApReason::Setup);
        return;
    }

    if (!s_apActive && now - s_staDownSinceMs > kApFallbackAfterMs) startAp(ApReason::Fallback);

    const uint32_t retry =
        (s_apActive && WiFi.softAPgetStationNum() > 0) ? kStaRetryWithApClientsMs : kStaRetryMs;
    if (now - s_lastStaAttemptMs > retry) {
        WiFi.disconnect(false, false);
        beginSta();
    }
}

void wifiApplyConfig() {
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

void wifiOpenSetupAp() { startAp(ApReason::Manual); }

bool wifiStaConnected() { return WiFi.status() == WL_CONNECTED; }

WifiStatus wifiStatus() {
    WifiStatus s;
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

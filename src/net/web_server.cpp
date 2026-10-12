#include "web_server.h"
#include "config/config.h"
#include "hal/board.h"
#include "hal/display.h"
#include "net/wifi_manager.h"
#include "net/web_ui.h"
#include "version.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {

WebServer s_server(80);
bool s_pendingWifiApply = false;
uint32_t s_rebootAtMs = 0;
bool s_factoryReset = false;

bool authorized() {
    if (g_config.adminPassword.length() == 0) return true;
    if (s_server.authenticate("admin", g_config.adminPassword.c_str())) return true;
    s_server.requestAuthentication(BASIC_AUTH, "rhino-aprs");
    return false;
}

void sendJson(int code, const JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(code, "application/json", out);
}

void sendError(int code, const String &msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    sendJson(code, doc);
}

void sendOk() {
    JsonDocument doc;
    doc["ok"] = true;
    sendJson(200, doc);
}

void handleRoot() {
    if (!authorized()) return;
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, "text/html", kWebUiHtml);
}

void handleGetConfig() {
    if (!authorized()) return;
    JsonDocument doc;
    configToJson(g_config, doc.to<JsonObject>());
    sendJson(200, doc);
}

void handlePostConfig() {
    if (!authorized()) return;
    JsonDocument in;
    DeserializationError err = deserializeJson(in, s_server.arg("plain"));
    if (err) {
        sendError(400, String("Bad JSON: ") + err.c_str());
        return;
    }
    AppConfig next = g_config;
    configFromJson(next, in.as<JsonObjectConst>());
    const String problem = configValidate(next);
    if (problem.length()) {
        sendError(422, problem);
        return;
    }
    const bool wifiChanged = next.wifiSsid != g_config.wifiSsid ||
                             next.wifiPassword != g_config.wifiPassword ||
                             next.hostname != g_config.hostname;
    const bool tzChanged = next.timezone != g_config.timezone;
    g_config = next;
    displaySetBrightness(g_config.brightness);
    if (tzChanged) {
        setenv("TZ", g_config.timezone.c_str(), 1);
        tzset();
    }
    if (!configSave()) {
        sendError(500, "Failed to write config");
        return;
    }
    if (wifiChanged) s_pendingWifiApply = true;
    JsonDocument doc;
    doc["ok"] = true;
    doc["wifiChanged"] = wifiChanged;
    sendJson(200, doc);
}

void handleStatus() {
    if (!authorized()) return;
    const WifiStatus w = wifiStatus();
    JsonDocument doc;
    doc["app"] = APP_NAME;
    doc["version"] = APP_VERSION;
    doc["callsign"] = g_config.fullCallsign();
    doc["uptimeS"] = millis() / 1000;
    doc["heapFree"] = ESP.getFreeHeap();
    doc["psramFree"] = ESP.getFreePsram();
    doc["batteryMv"] = boardBatteryMilliVolts();
    doc["batteryPct"] = boardBatteryPercent();
    JsonObject wj = doc["wifi"].to<JsonObject>();
    wj["apActive"] = w.apActive;
    wj["apReason"] = apReasonStr(w.apReason);
    wj["apSsid"] = w.apSsid;
    wj["apIp"] = w.apIp;
    wj["apClients"] = w.apClients;
    wj["staConfigured"] = w.staConfigured;
    wj["staConnected"] = w.staConnected;
    wj["staSsid"] = w.staSsid;
    wj["staIp"] = w.staIp;
    wj["rssi"] = w.rssi;
    wj["hostname"] = w.hostname;
    doc["tnc"]["state"] = "not implemented";
    sendJson(200, doc);
}

void handleWifiScan() {
    if (!authorized()) return;
    JsonDocument doc;
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        doc["scanning"] = true;
    } else if (n == WIFI_SCAN_FAILED || s_server.hasArg("start")) {
        WiFi.scanNetworks(true, false);
        doc["scanning"] = true;
    } else {
        doc["scanning"] = false;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            bool dup = false;
            for (JsonObject o : arr) {
                if (ssid == o["ssid"].as<const char *>()) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
            o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
    }
    sendJson(200, doc);
}

void handleReboot() {
    if (!authorized()) return;
    sendOk();
    s_rebootAtMs = millis() + 750;
}

void handleFactoryReset() {
    if (!authorized()) return;
    sendOk();
    s_factoryReset = true;
    s_rebootAtMs = millis() + 750;
}

void handleNotFound() {
    s_server.send(404, "text/plain", "Not found");
}

}  // namespace

void webBegin() {
    s_server.on("/", HTTP_GET, handleRoot);
    s_server.on("/api/config", HTTP_GET, handleGetConfig);
    s_server.on("/api/config", HTTP_POST, handlePostConfig);
    s_server.on("/api/status", HTTP_GET, handleStatus);
    s_server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
    s_server.on("/api/reboot", HTTP_POST, handleReboot);
    s_server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
    s_server.onNotFound(handleNotFound);
    s_server.begin();
    Serial.println("[web] server listening on :80");
}

void webLoop() {
    s_server.handleClient();
    if (s_pendingWifiApply) {
        s_pendingWifiApply = false;
        wifiApplyConfig();
    }
    if (s_rebootAtMs && (int32_t)(millis() - s_rebootAtMs) >= 0) {
        if (s_factoryReset) {
            configResetToDefaults();
            configSave();
        }
        ESP.restart();
    }
}

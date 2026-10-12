#include "web_server.h"
#include "config/config.h"
#include "hal/board.h"
#include "hal/display.h"
#include "hal/sd_card.h"
#include "hal/gps.h"
#include "services/backup.h"
#include "services/beacon.h"
#include "services/messages.h"
#include "services/data_store.h"
#include "services/router.h"
#include "transport/ble_kiss.h"
#include "util/geo.h"
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
    const bool tncChanged = next.tncAddress != g_config.tncAddress ||
                            next.tncAddressType != g_config.tncAddressType ||
                            next.tncServiceUuid != g_config.tncServiceUuid ||
                            next.tncWriteUuid != g_config.tncWriteUuid ||
                            next.tncNotifyUuid != g_config.tncNotifyUuid ||
                            next.tncAutoConnect != g_config.tncAutoConnect;
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
    if (tncChanged) g_ble.reconfigure();
    JsonDocument doc;
    doc["ok"] = true;
    doc["wifiChanged"] = wifiChanged;
    sendJson(200, doc);
}

void fillTncStatus(JsonObject t) {
    const BleStatus b = g_ble.status();
    t["state"] = bleStateName(b.state);
    t["connected"] = b.state == BleState::Connected;
    t["scanning"] = b.scanning;
    t["name"] = b.peerName;
    t["address"] = b.peerAddress;
    t["lastError"] = b.lastError;
    t["mtu"] = b.mtu;
    t["rxFrames"] = b.rxFrames;
    t["txFrames"] = b.txFrames;
    t["retryInS"] = b.nextAttemptInS;
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
    fillTncStatus(doc["tnc"].to<JsonObject>());
    JsonObject sd = doc["sd"].to<JsonObject>();
    sd["mounted"] = sdMounted();
    sd["totalMB"] = (uint32_t)(sdTotalBytes() / (1024 * 1024));
    sd["usedMB"] = (uint32_t)(sdUsedBytes() / (1024 * 1024));
    doc["stations"] = stationDbCount();
    doc["packets"] = packetLogLastSeq();
    doc["unreadMessages"] = messagesUnread();
    doc["messagesGen"] = g_msgGen;
    const GpsFix f = gpsFix();
    JsonObject g = doc["gps"].to<JsonObject>();
    g["enabled"] = f.enabled;
    g["alive"] = f.alive;
    g["fix"] = f.valid;
    g["sats"] = f.sats;
    if (f.valid) {
        g["lat"] = f.lat;
        g["lon"] = f.lon;
        g["hdop"] = f.hdop;
        g["speedKmh"] = f.speedKmh;
        g["course"] = f.course;
        if (f.hasAlt) g["altM"] = f.altM;
    }
    const BeaconStatus b = beaconStatus();
    JsonObject bj = doc["beacon"].to<JsonObject>();
    bj["mode"] = b.mode;
    if (b.sentAny) bj["lastAgoS"] = b.lastAgoS;
    if (b.nextInS >= 0) bj["nextInS"] = b.nextInS;
    bj["waitingFor"] = b.waitingFor;
    bj["lastError"] = b.lastError;
    sendJson(200, doc);
}

// ── TNC ──────────────────────────────────────────────────────────────────────

void handleTncStatus() {
    if (!authorized()) return;
    JsonDocument doc;
    fillTncStatus(doc.to<JsonObject>());
    sendJson(200, doc);
}

void handleTncScan() {
    if (!authorized()) return;
    if (s_server.hasArg("start")) g_ble.startScan(8);
    JsonDocument doc;
    doc["scanning"] = g_ble.status().scanning;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (const auto &r : g_ble.scanResults()) {
        JsonObject o = arr.add<JsonObject>();
        o["address"] = r.address;
        o["addressType"] = r.addressType;
        o["name"] = r.name;
        o["rssi"] = r.rssi;
        o["kiss"] = r.kissService;
    }
    sendJson(200, doc);
}

void handleTncSelect() {
    if (!authorized()) return;
    JsonDocument in;
    if (deserializeJson(in, s_server.arg("plain"))) {
        sendError(400, "Bad JSON");
        return;
    }
    String addr = in["address"] | "";
    addr.trim();
    addr.toLowerCase();
    if (addr.length() != 17) {
        sendError(422, "Invalid BLE address");
        return;
    }
    g_config.tncAddress = addr;
    g_config.tncAddressType = in["addressType"] | 0;
    g_config.tncName = in["name"] | "";
    if (!configSave()) {
        sendError(500, "Failed to write config");
        return;
    }
    g_ble.connectNow();
    sendOk();
}

void handleTncConnect() {
    if (!authorized()) return;
    if (!g_config.tncAddress.length()) {
        sendError(422, "No TNC selected");
        return;
    }
    g_ble.connectNow();
    sendOk();
}

void handleTncDisconnect() {
    if (!authorized()) return;
    g_ble.disconnect();
    sendOk();
}

// ── Beacon / messages ────────────────────────────────────────────────────────

void handleBeaconNow() {
    if (!authorized()) return;
    String err;
    if (!beaconNow(err)) {
        sendError(422, err);
        return;
    }
    sendOk();
}

void handleMessages() {
    if (!authorized()) return;
    const uint32_t now = uptimeS();
    JsonDocument doc;
    doc["gen"] = g_msgGen;
    doc["me"] = g_config.fullCallsign();
    JsonArray th = doc["threads"].to<JsonArray>();
    for (const ChatThread &t : messagesThreads()) {
        JsonObject o = th.add<JsonObject>();
        o["peer"] = t.peer;
        o["lastText"] = t.lastText;
        o["lastOut"] = t.lastOutgoing;
        o["unread"] = t.unread;
        o["count"] = t.count;
        if (t.lastEpoch) o["epoch"] = (long long)t.lastEpoch;
        else if (t.lastUptimeS) o["ageS"] = now - t.lastUptimeS;
    }
    const String peer = s_server.arg("peer");
    JsonArray ms = doc["messages"].to<JsonArray>();
    for (size_t i = 0; i < messagesCount(); i++) {
        const ChatMsg *m = messagesAt(i);
        if (peer.length() && !peer.equalsIgnoreCase(m->peer)) continue;
        JsonObject o = ms.add<JsonObject>();
        o["seq"] = m->seq;
        o["peer"] = m->peer;
        o["out"] = m->outgoing;
        o["state"] = msgStateName(m->state);
        o["tries"] = m->tries;
        o["text"] = m->text;
        if (m->epoch) o["epoch"] = (long long)m->epoch;
        else if (m->uptimeS) o["ageS"] = now - m->uptimeS;
    }
    sendJson(200, doc);
}

void handleMessageSend() {
    if (!authorized()) return;
    JsonDocument in;
    if (deserializeJson(in, s_server.arg("plain"))) {
        sendError(400, "Bad JSON");
        return;
    }
    String err;
    if (!messagesSend(in["to"] | "", in["text"] | "", err)) {
        sendError(422, err);
        return;
    }
    sendOk();
}

void handleMessageRead() {
    if (!authorized()) return;
    messagesMarkRead(s_server.arg("peer"));
    sendOk();
}

void handleMessageDelete() {
    if (!authorized()) return;
    messagesDeleteThread(s_server.arg("peer"));
    sendOk();
}

void handleMessageRetry() {
    if (!authorized()) return;
    String err;
    if (!messagesRetry(s_server.arg("seq").toInt(), err)) {
        sendError(404, err);
        return;
    }
    sendOk();
}

// ── Packets / stations ───────────────────────────────────────────────────────

void handlePackets() {
    if (!authorized()) return;
    const uint32_t since = s_server.hasArg("since") ? s_server.arg("since").toInt() : 0;
    JsonDocument doc;
    doc["last"] = packetLogLastSeq();
    doc["now"] = uptimeS();
    JsonArray arr = doc["packets"].to<JsonArray>();
    for (size_t i = 0; i < packetLogCount() && i < 100; i++) {
        const LogEntry *e = packetLogAt(i);
        if (e->seq <= since) break;
        JsonObject o = arr.add<JsonObject>();
        o["seq"] = e->seq;
        o["ageS"] = uptimeS() - e->uptimeS;
        if (e->epoch) o["epoch"] = (long long)e->epoch;
        o["tx"] = e->tx;
        o["is"] = e->viaIs;
        o["raw"] = e->raw;
    }
    sendJson(200, doc);
}

void handleStations() {
    if (!authorized()) return;
    double myLat, myLon;
    const bool havePos = myPosition(myLat, myLon);
    const bool metric = g_config.units == Units::Metric;
    JsonDocument doc;
    JsonArray arr = doc["stations"].to<JsonArray>();
    for (size_t i = 0; i < stationDbCount(); i++) {
        const Station *s = stationDbAt(i);
        JsonObject o = arr.add<JsonObject>();
        o["call"] = s->call;
        if (s->isObject) o["owner"] = s->owner;
        o["ageS"] = uptimeS() - s->lastHeardS;
        o["packets"] = s->packets;
        o["type"] = aprs::typeName(s->lastType);
        o["symbol"] = String(s->symTable) + String(s->symCode);
        if (s->hasPos) {
            o["lat"] = s->lat;
            o["lon"] = s->lon;
            if (havePos) {
                const double km = geo::distanceKm(myLat, myLon, s->lat, s->lon);
                o["distance"] = geo::formatDistance(km, metric);
                o["bearing"] = (int)geo::bearingDeg(myLat, myLon, s->lat, s->lon);
            }
        }
        if (s->hasCourseSpeed) o["speed"] = geo::formatSpeed(s->speedKnots, metric);
        if (s->hasAlt) o["altitude"] = geo::formatAltitude(s->altFeet, metric);
        o["comment"] = s->comment;
        o["status"] = s->status;
        o["path"] = s->path;
    }
    sendJson(200, doc);
}

// ── Backup / restore ─────────────────────────────────────────────────────────

String backupFileName() {
    char name[48];
    const time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(name, sizeof(name), "rhino-aprs-%Y%m%d-%H%M%S.json", &tm);
    } else {
        snprintf(name, sizeof(name), "rhino-aprs-backup.json");
    }
    return name;
}

void sendAttachment(const String &name, const String &body) {
    s_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", body);
}

void handleBackupDownload() {
    if (!authorized()) return;
    JsonDocument doc;
    backupBuild(doc, s_server.arg("secrets") == "1");
    String out;
    serializeJsonPretty(doc, out);
    sendAttachment(backupFileName(), out);
}

void restoreAndReboot(JsonDocument &doc) {
    String err;
    if (!backupRestore(doc.as<JsonObjectConst>(), err)) {
        sendError(422, err);
        return;
    }
    sendOk();
    s_rebootAtMs = millis() + 1000;
}

void handleRestoreUpload() {
    if (!authorized()) return;
    JsonDocument doc;
    const DeserializationError e = deserializeJson(doc, s_server.arg("plain"));
    if (e) {
        sendError(400, String("Not valid JSON: ") + e.c_str());
        return;
    }
    restoreAndReboot(doc);
}

void handleSdBackupList() {
    if (!authorized()) return;
    JsonDocument doc;
    const auto files = backupListSd();
    doc["mounted"] = sdMounted();
    JsonArray arr = doc["files"].to<JsonArray>();
    for (const auto &f : files) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = f.name;
        o["size"] = f.size;
    }
    sendJson(200, doc);
}

void handleSdBackupSave() {
    if (!authorized()) return;
    String name, err;
    if (!backupSaveToSd(s_server.arg("secrets") == "1", name, err)) {
        sendError(500, err);
        return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["name"] = name;
    sendJson(200, doc);
}

void handleSdBackupRestore() {
    if (!authorized()) return;
    JsonDocument doc;
    String err;
    if (!backupReadSd(s_server.arg("name"), doc, err)) {
        sendError(404, err);
        return;
    }
    restoreAndReboot(doc);
}

void handleSdBackupDownload() {
    if (!authorized()) return;
    JsonDocument doc;
    String err;
    const String name = s_server.arg("name");
    if (!backupReadSd(name, doc, err)) {
        sendError(404, err);
        return;
    }
    String out;
    serializeJsonPretty(doc, out);
    sendAttachment(name, out);
}

void handleSdBackupDelete() {
    if (!authorized()) return;
    if (!backupDeleteSd(s_server.arg("name"))) {
        sendError(404, "Delete failed");
        return;
    }
    sendOk();
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
    s_server.on("/api/tnc/status", HTTP_GET, handleTncStatus);
    s_server.on("/api/tnc/scan", HTTP_GET, handleTncScan);
    s_server.on("/api/tnc/select", HTTP_POST, handleTncSelect);
    s_server.on("/api/tnc/connect", HTTP_POST, handleTncConnect);
    s_server.on("/api/tnc/disconnect", HTTP_POST, handleTncDisconnect);
    s_server.on("/api/beacon", HTTP_POST, handleBeaconNow);
    s_server.on("/api/messages", HTTP_GET, handleMessages);
    s_server.on("/api/messages", HTTP_POST, handleMessageSend);
    s_server.on("/api/messages/read", HTTP_POST, handleMessageRead);
    s_server.on("/api/messages/delete", HTTP_POST, handleMessageDelete);
    s_server.on("/api/messages/retry", HTTP_POST, handleMessageRetry);
    s_server.on("/api/packets", HTTP_GET, handlePackets);
    s_server.on("/api/stations", HTTP_GET, handleStations);
    s_server.on("/api/backup", HTTP_GET, handleBackupDownload);
    s_server.on("/api/restore", HTTP_POST, handleRestoreUpload);
    s_server.on("/api/backup/sd", HTTP_GET, handleSdBackupList);
    s_server.on("/api/backup/sd", HTTP_POST, handleSdBackupSave);
    s_server.on("/api/backup/sd/restore", HTTP_POST, handleSdBackupRestore);
    s_server.on("/api/backup/sd/download", HTTP_GET, handleSdBackupDownload);
    s_server.on("/api/backup/sd/delete", HTTP_POST, handleSdBackupDelete);
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

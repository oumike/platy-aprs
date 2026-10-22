#include <lvgl.h>
#include "web_server.h"
#include "config/config.h"
#include "services/notify_led.h"
#include "hal/audio.h"
#include "hal/board.h"
#include "hal/board_pins.h"
#include "hal/display.h"
#include "hal/screen.h"
#include "hal/sd_card.h"
#include "hal/gps.h"
#include "services/backup.h"
#include "services/beacon.h"
#include "services/favorites.h"
#include "services/messages.h"
#include "services/ota.h"
#include "services/data_store.h"
#include "services/router.h"
#include "services/sd_log.h"
#include "transport/ble_kiss.h"
#include "transport/lora_aprs.h"
#include "util/geo.h"
#include "net/wifi_manager.h"
#include "net/aprs_is.h"
#include "net/web_ui.h"
#include "ui/ui.h"
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
    // CSRF guard: a page on another origin cannot add a custom header without a
    // CORS preflight, which this server never answers. Browsers resend cached
    // Basic auth credentials cross-site, so the password alone does not stop
    // a forged POST.
    if (s_server.method() == HTTP_POST && !s_server.hasHeader("X-Platy")) {
        s_server.send(403, "text/plain", "Missing X-Platy header");
        return false;
    }
    if (g_config.adminPassword.length() == 0) return true;
    if (s_server.authenticate("admin", g_config.adminPassword.c_str())) return true;
    s_server.requestAuthentication(BASIC_AUTH, APP_NAME);
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
    JsonObject caps = doc["caps"].to<JsonObject>();
    caps["sound"] = audioPresent();
    caps["volume"] = audioHasVolume();
    caps["led"] = notifyLedPresent();
    caps["kbBlink"] = notifyKbBlinkPresent();
    caps["orientation"] = displayCanRotate();
#if HAS_EINK
    caps["theme"] = false;   // e-paper: always the light monochrome theme
    caps["minBrightness"] = 0;
#endif
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
                            next.tncAutoConnect != g_config.tncAutoConnect ||
                            next.rfLink != g_config.rfLink;
    const bool loraChanged = next.rfLink != g_config.rfLink || next.loraFreqMhz != g_config.loraFreqMhz ||
                             next.loraSf != g_config.loraSf || next.loraBwKhz != g_config.loraBwKhz ||
                             next.loraCr != g_config.loraCr || next.loraPowerDbm != g_config.loraPowerDbm;
    g_config = next;
    screenApplyBrightness();
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
    if (loraChanged) g_lora.reconfigure();
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
    {
        const LoraStatus l = g_lora.status();
        JsonObject lo = doc["lora"].to<JsonObject>();
        lo["state"] = loraStateName(l.state);
        lo["active"] = g_config.rfLink == RfLink::Lora;
        lo["freqMhz"] = l.freqMhz;
        lo["rxFrames"] = l.rxFrames;
        lo["txFrames"] = l.txFrames;
        lo["rxBad"] = l.rxBad;
        if (l.lastRxS) {
            lo["lastRxAgoS"] = uptimeS() - l.lastRxS;
            lo["lastRssi"] = l.lastRssi;
            lo["lastSnr"] = l.lastSnr;
        }
        lo["lastError"] = l.lastError;
    }
    JsonObject sd = doc["sd"].to<JsonObject>();
    sd["slot"] = sdHasSlot();
    sd["mounted"] = sdMounted();
    sd["status"] = sdStatusText();
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
    const AprsIsStatus is = aprsIsStatus();
    JsonObject ij = doc["aprsis"].to<JsonObject>();
    ij["state"] = is.state;
    ij["connected"] = is.connected;
    ij["verified"] = is.verified;
    ij["server"] = is.server;
    ij["lastError"] = is.lastError;
    ij["rxLines"] = is.rxLines;
    ij["txLines"] = is.txLines;
    ij["gatedRfToIs"] = is.gatedRfToIs;
    ij["gatedIsToRf"] = is.gatedIsToRf;
    ij["connectedForS"] = is.connectedForS;
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

void handleTncBonds() {
    if (!authorized()) return;
    JsonDocument doc;
    JsonArray arr = doc["bonds"].to<JsonArray>();
    for (const auto &b : g_ble.bonds()) {
        JsonObject o = arr.add<JsonObject>();
        o["address"] = b.address;
        o["addressType"] = b.addressType;
        o["saved"] = b.address.equalsIgnoreCase(g_config.tncAddress);
    }
    doc["savedAddress"] = g_config.tncAddress;
    doc["savedName"] = g_config.tncName;
    sendJson(200, doc);
}

// Body: {"address": "...", "addressType": n} deletes one bond; empty address deletes all.
// {"removeSaved": true} also clears the saved radio selection.
void handleTncForget() {
    if (!authorized()) return;
    JsonDocument in;
    if (s_server.arg("plain").length() && deserializeJson(in, s_server.arg("plain"))) {
        sendError(400, "Bad JSON");
        return;
    }
    if (in["removeSaved"] | false) {
        if (!g_config.tncAddress.length()) {
            sendError(422, "No saved radio");
            return;
        }
        g_ble.forgetBond(g_config.tncAddress, g_config.tncAddressType);
        g_config.tncAddress = "";
        g_config.tncName = "";
        if (!configSave()) {
            sendError(500, "Failed to write config");
            return;
        }
        g_ble.reconfigure();
        sendOk();
        return;
    }
    String addr = in["address"] | "";
    addr.trim();
    addr.toLowerCase();
    if (addr.length() && addr.length() != 17) {
        sendError(422, "Invalid BLE address");
        return;
    }
    g_ble.forgetBond(addr, in["addressType"] | 0);
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
        if (favIs(t.peer.c_str())) o["fav"] = true;
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
        if (!m->outgoing && m->via != MsgVia::Unknown) {
            o["via"] = msgViaName(m->via);
            o["path"] = m->path;
            if (m->relay[0]) o["relay"] = m->relay;
        }
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

void handleFavoriteToggle() {
    if (!authorized()) return;
    const String call = s_server.arg("call");
    if (!call.length() || call.length() > 9) {
        s_server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad call\"}");
        return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["fav"] = favToggle(call.c_str());
    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
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
        if (favIs(s->call)) o["fav"] = true;
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
        strftime(name, sizeof(name), "platy-aprs-%Y%m%d-%H%M%S.json", &tm);
    } else {
        snprintf(name, sizeof(name), "platy-aprs-backup.json");
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
    doc["slot"] = sdHasSlot();
    doc["mounted"] = sdMounted();
    doc["status"] = sdStatusText();
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

void handleLogList() {
    if (!authorized()) return;
    JsonDocument doc;
    doc["mounted"] = sdMounted();
    JsonArray arr = doc["files"].to<JsonArray>();
    for (const auto &f : sdLogList()) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = f.name;
        o["size"] = f.size;
    }
    sendJson(200, doc);
}

void handleLogDownload() {
    if (!authorized()) return;
    sdLogList();   // flushes buffered lines
    const String name = s_server.arg("name");
    const String path = sdLogPath(name);
    File f = path.length() && sdMounted() ? sdFs().open(path, FILE_READ) : File();
    if (!f || f.isDirectory()) {
        sendError(404, "No such log");
        return;
    }
    s_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    s_server.streamFile(f, "text/plain");
    f.close();
}

void handleLogDelete() {
    if (!authorized()) return;
    if (!sdLogDelete(s_server.arg("name"))) {
        sendError(404, "Delete failed");
        return;
    }
    sendOk();
}

void handleWifiScan() {
    if (!authorized()) return;
    JsonDocument doc;
    std::vector<WifiNetwork> nets;
    if (wifiScanRunning()) {
        doc["scanning"] = true;
    } else if (s_server.hasArg("start") || !wifiScanResults(nets)) {
        wifiScanStart();
        doc["scanning"] = true;
    } else {
        doc["scanning"] = false;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (const WifiNetwork &w : nets) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = w.ssid;
            o["rssi"] = w.rssi;
            o["secure"] = w.secure;
        }
    }
    sendJson(200, doc);
}

void sendOtaStatus() {
    const OtaStatus o = otaStatus();
    JsonDocument doc;
    doc["running"] = APP_VERSION;
    doc["board"] = otaAssetSlug();
    doc["state"] = otaStateName(o.state);
    doc["latest"] = o.latest;
    doc["error"] = o.error;
    doc["written"] = o.written;
    doc["total"] = o.total;
    doc["layoutOk"] = o.layoutOk;
    sendJson(200, doc);
}

void handleOtaStatus() {
    if (!authorized()) return;
    sendOtaStatus();
}

void handleOtaCheck() {
    if (!authorized()) return;
    if (!otaStartCheck()) {
        sendError(409, "Can't check now (busy, or not on Wi-Fi)");
        return;
    }
    sendOtaStatus();
}

void handleOtaInstall() {
    if (!authorized()) return;
    if (!otaStartInstall()) {
        sendError(409, "Can't install now (busy, or not on Wi-Fi)");
        return;
    }
    sendOtaStatus();
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
    static const char *kHeaders[] = {"X-Platy"};
    s_server.collectHeaders(kHeaders, 1);
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
    s_server.on("/api/tnc/bonds", HTTP_GET, handleTncBonds);
    s_server.on("/api/tnc/forget", HTTP_POST, handleTncForget);
    s_server.on("/api/beacon", HTTP_POST, handleBeaconNow);
    s_server.on("/api/messages", HTTP_GET, handleMessages);
    s_server.on("/api/messages", HTTP_POST, handleMessageSend);
    s_server.on("/api/messages/read", HTTP_POST, handleMessageRead);
    s_server.on("/api/messages/delete", HTTP_POST, handleMessageDelete);
    s_server.on("/api/favorites/toggle", HTTP_POST, handleFavoriteToggle);
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
#ifdef PLATY_SERIAL_KEYS
    // Debug builds: raw RGB565 dump of the active screen (240x320, little endian).
    // ?splash=1 renders the splash off-screen instead (for previews).
    // ?layer=top dumps the top layer (status bar, overlays) as ARGB8888 to
    // composite over the screen.
    s_server.on("/api/debug/screen", HTTP_GET, [] {
        if (!authorized()) return;
        lv_obj_t *splash = nullptr;
        if (s_server.arg("splash") == "1") {
            splash = uiBuildSplash(nullptr);
            lv_obj_update_layout(splash);
        }
        const bool top = s_server.arg("layer") == "top";
        lv_draw_buf_t *b = lv_snapshot_take(top ? lv_layer_top() : splash ? splash : lv_screen_active(),
                                            top ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565);
        if (splash) lv_obj_delete(splash);
        if (!b) { s_server.send(500, "text/plain", "snapshot failed"); return; }
        s_server.setContentLength(b->header.h * b->header.stride);
        s_server.send(200, "application/octet-stream", "");
        s_server.sendContent((const char *)b->data, b->header.h * b->header.stride);
        lv_draw_buf_destroy(b);
    });
#endif
    s_server.on("/api/logs", HTTP_GET, handleLogList);
    s_server.on("/api/logs/download", HTTP_GET, handleLogDownload);
    s_server.on("/api/logs/delete", HTTP_POST, handleLogDelete);
    s_server.on("/api/ota", HTTP_GET, handleOtaStatus);
    s_server.on("/api/ota/check", HTTP_POST, handleOtaCheck);
    s_server.on("/api/ota/install", HTTP_POST, handleOtaInstall);
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

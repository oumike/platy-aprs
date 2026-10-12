#include "config.h"
#include "aprs/passcode.h"
#include <LittleFS.h>
#include <sys/stat.h>

AppConfig g_config;

static const char *kPath = "/config.json";

String AppConfig::fullCallsign() const {
    String s = callsign;
    s.toUpperCase();
    if (ssid > 0) s += "-" + String(ssid);
    return s;
}

int32_t AppConfig::effectivePasscode() const {
    return aprsIsPasscode >= 0 ? aprsIsPasscode : aprsPasscode(callsign.c_str());
}

static const char *beaconModeStr(BeaconMode m) {
    switch (m) {
        case BeaconMode::Gps: return "gps";
        case BeaconMode::Fixed: return "fixed";
        default: return "off";
    }
}

static BeaconMode beaconModeFrom(const char *s, BeaconMode def) {
    if (!s) return def;
    if (!strcmp(s, "gps")) return BeaconMode::Gps;
    if (!strcmp(s, "fixed")) return BeaconMode::Fixed;
    if (!strcmp(s, "off")) return BeaconMode::Off;
    return def;
}

void configToJson(const AppConfig &c, JsonObject o) {
    JsonObject st = o["station"].to<JsonObject>();
    st["callsign"] = c.callsign;
    st["ssid"] = c.ssid;
    st["symbolTable"] = String(c.symbolTable);
    st["symbolCode"] = String(c.symbolCode);
    st["comment"] = c.comment;
    st["path"] = c.path;

    JsonObject b = o["beacon"].to<JsonObject>();
    b["mode"] = beaconModeStr(c.beaconMode);
    b["lat"] = c.fixedLat;
    b["lon"] = c.fixedLon;
    b["altM"] = c.fixedAltM;
    b["intervalS"] = c.beaconIntervalS;
    JsonObject sb = b["smart"].to<JsonObject>();
    sb["enabled"] = c.smart.enabled;
    sb["slowRateS"] = c.smart.slowRateS;
    sb["fastRateS"] = c.smart.fastRateS;
    sb["slowSpeedKmh"] = c.smart.slowSpeedKmh;
    sb["fastSpeedKmh"] = c.smart.fastSpeedKmh;
    sb["minTurnDeg"] = c.smart.minTurnDeg;
    sb["turnSlope"] = c.smart.turnSlope;
    sb["minTurnTimeS"] = c.smart.minTurnTimeS;

    JsonObject is = o["aprsis"].to<JsonObject>();
    is["enabled"] = c.aprsIsEnabled;
    is["server"] = c.aprsIsServer;
    is["port"] = c.aprsIsPort;
    is["passcode"] = c.aprsIsPasscode;
    is["computedPasscode"] = aprsPasscode(c.callsign.c_str());
    is["filter"] = c.aprsIsFilter;
    is["rfToIs"] = c.igateRfToIs;
    is["isToRfMsgs"] = c.igateIsToRfMsgs;

    o["digi"]["enabled"] = c.digiEnabled;

    JsonObject t = o["tnc"].to<JsonObject>();
    t["name"] = c.tncName;
    t["address"] = c.tncAddress;
    t["addressType"] = c.tncAddressType;
    t["serviceUuid"] = c.tncServiceUuid;
    t["writeUuid"] = c.tncWriteUuid;
    t["notifyUuid"] = c.tncNotifyUuid;
    t["autoConnect"] = c.tncAutoConnect;

    JsonObject w = o["wifi"].to<JsonObject>();
    w["ssid"] = c.wifiSsid;
    w["passwordSet"] = c.wifiPassword.length() > 0;
    w["hostname"] = c.hostname;

    JsonObject d = o["device"].to<JsonObject>();
    d["units"] = c.units == Units::Metric ? "metric" : "imperial";
    d["buzzer"] = c.buzzer;
    d["brightness"] = c.brightness;
    d["timezone"] = c.timezone;
    d["adminPasswordSet"] = c.adminPassword.length() > 0;
}

template <typename T>
static void take(JsonObjectConst o, const char *key, T &dst) {
    JsonVariantConst v = o[key];
    if (!v.isNull() && v.is<T>()) dst = v.as<T>();
}

static void takeStr(JsonObjectConst o, const char *key, String &dst) {
    JsonVariantConst v = o[key];
    if (v.is<const char *>()) dst = v.as<const char *>();
}

static void takeChar(JsonObjectConst o, const char *key, char &dst) {
    JsonVariantConst v = o[key];
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (s && s[0]) dst = s[0];
    }
}

template <typename T>
static void takeNum(JsonObjectConst o, const char *key, T &dst) {
    JsonVariantConst v = o[key];
    if (v.is<double>() || v.is<long>()) dst = v.as<T>();
}

void configFromJson(AppConfig &c, JsonObjectConst o) {
    JsonObjectConst st = o["station"];
    if (st) {
        takeStr(st, "callsign", c.callsign);
        c.callsign.trim();
        c.callsign.toUpperCase();
        takeNum(st, "ssid", c.ssid);
        takeChar(st, "symbolTable", c.symbolTable);
        takeChar(st, "symbolCode", c.symbolCode);
        takeStr(st, "comment", c.comment);
        takeStr(st, "path", c.path);
        c.path.trim();
        c.path.toUpperCase();
    }
    JsonObjectConst b = o["beacon"];
    if (b) {
        c.beaconMode = beaconModeFrom(b["mode"] | (const char *)nullptr, c.beaconMode);
        takeNum(b, "lat", c.fixedLat);
        takeNum(b, "lon", c.fixedLon);
        takeNum(b, "altM", c.fixedAltM);
        takeNum(b, "intervalS", c.beaconIntervalS);
        JsonObjectConst sb = b["smart"];
        if (sb) {
            take(sb, "enabled", c.smart.enabled);
            takeNum(sb, "slowRateS", c.smart.slowRateS);
            takeNum(sb, "fastRateS", c.smart.fastRateS);
            takeNum(sb, "slowSpeedKmh", c.smart.slowSpeedKmh);
            takeNum(sb, "fastSpeedKmh", c.smart.fastSpeedKmh);
            takeNum(sb, "minTurnDeg", c.smart.minTurnDeg);
            takeNum(sb, "turnSlope", c.smart.turnSlope);
            takeNum(sb, "minTurnTimeS", c.smart.minTurnTimeS);
        }
    }
    JsonObjectConst is = o["aprsis"];
    if (is) {
        take(is, "enabled", c.aprsIsEnabled);
        takeStr(is, "server", c.aprsIsServer);
        takeNum(is, "port", c.aprsIsPort);
        takeNum(is, "passcode", c.aprsIsPasscode);
        takeStr(is, "filter", c.aprsIsFilter);
        take(is, "rfToIs", c.igateRfToIs);
        take(is, "isToRfMsgs", c.igateIsToRfMsgs);
    }
    JsonObjectConst dg = o["digi"];
    if (dg) take(dg, "enabled", c.digiEnabled);

    JsonObjectConst t = o["tnc"];
    if (t) {
        takeStr(t, "name", c.tncName);
        takeStr(t, "address", c.tncAddress);
        takeNum(t, "addressType", c.tncAddressType);
        takeStr(t, "serviceUuid", c.tncServiceUuid);
        takeStr(t, "writeUuid", c.tncWriteUuid);
        takeStr(t, "notifyUuid", c.tncNotifyUuid);
        take(t, "autoConnect", c.tncAutoConnect);
    }
    JsonObjectConst w = o["wifi"];
    if (w) {
        takeStr(w, "ssid", c.wifiSsid);
        String pw;
        takeStr(w, "password", pw);
        if (pw.length()) c.wifiPassword = pw;
        if (w["clearPassword"] | false) c.wifiPassword = "";
        takeStr(w, "hostname", c.hostname);
    }
    JsonObjectConst d = o["device"];
    if (d) {
        const char *u = d["units"] | (const char *)nullptr;
        if (u) c.units = strcmp(u, "metric") == 0 ? Units::Metric : Units::Imperial;
        take(d, "buzzer", c.buzzer);
        if (d["brightness"].is<long>()) {
            const long b = d["brightness"].as<long>();
            c.brightness = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
        }
        takeStr(d, "timezone", c.timezone);
        String pw;
        takeStr(d, "adminPassword", pw);
        if (pw.length()) c.adminPassword = pw;
        if (d["clearAdminPassword"] | false) c.adminPassword = "";
    }
}

String configValidate(const AppConfig &c) {
    if (c.callsign.length() < 3 || c.callsign.length() > 6) return "Callsign must be 3-6 characters";
    for (char ch : c.callsign) {
        if (!isalnum((unsigned char)ch)) return "Callsign may only contain letters and digits";
    }
    if (c.ssid > 15) return "SSID must be 0-15";
    if (c.symbolTable != '/' && c.symbolTable != '\\' && !isalnum((unsigned char)c.symbolTable))
        return "Symbol table must be '/', '\\' or an overlay character";
    if (c.symbolCode < '!' || c.symbolCode > '~') return "Invalid symbol code";
    if (c.comment.length() > 43) return "Comment is limited to 43 characters";
    if (c.fixedLat < -90 || c.fixedLat > 90) return "Latitude out of range";
    if (c.fixedLon < -180 || c.fixedLon > 180) return "Longitude out of range";
    if (c.beaconIntervalS != 0 && c.beaconIntervalS < 60) return "Beacon interval must be at least 60 s";
    if (c.smart.fastRateS < 30) return "SmartBeaconing fast rate must be at least 30 s";
    if (c.smart.slowSpeedKmh >= c.smart.fastSpeedKmh) return "SmartBeaconing slow speed must be below fast speed";
    if (c.aprsIsPasscode < -1 || c.aprsIsPasscode > 32767) return "Passcode must be -1 (auto) or 0-32767";
    if (c.wifiSsid.length() > 32) return "Wi-Fi SSID too long";
    if (c.wifiPassword.length() > 0 && c.wifiPassword.length() < 8) return "Wi-Fi password must be at least 8 characters";
    if (c.brightness < 10) return "Brightness must be 10-255";
    if (c.hostname.length() == 0 || c.hostname.length() > 32) return "Hostname must be 1-32 characters";
    return "";
}

void configResetToDefaults() { g_config = AppConfig(); }

bool configBegin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[config] LittleFS mount failed; using defaults");
        return false;
    }
    // stat() rather than LittleFS.exists(), which logs an error when the file is absent.
    struct stat st;
    File f = stat("/littlefs/config.json", &st) == 0 ? LittleFS.open(kPath, "r") : File();
    if (!f) {
        Serial.println("[config] no config yet; using defaults");
        return true;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[config] parse error %s; using defaults\n", err.c_str());
        return false;
    }
    // The stored file carries the secrets under the same keys the web API accepts.
    configFromJson(g_config, doc.as<JsonObjectConst>());
    Serial.printf("[config] loaded, station %s\n", g_config.fullCallsign().c_str());
    return true;
}

bool configSave() {
    JsonDocument doc;
    configToJson(g_config, doc.to<JsonObject>());
    doc["wifi"]["password"] = g_config.wifiPassword;
    doc["device"]["adminPassword"] = g_config.adminPassword;
    const char *tmp = "/config.json.tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    const size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    LittleFS.remove(kPath);
    return LittleFS.rename(tmp, kPath);
}

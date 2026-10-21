#include "backup.h"
#include <time.h>
#include <algorithm>
#include "config/config.h"
#include "hal/sd_card.h"
#include "version.h"

static const char *kFormat = "platy-aprs-backup";
static const char *kDir = "/platy/backups";
static constexpr int kVersion = 1;

void backupBuild(JsonDocument &doc, bool includeSecrets) {
    doc["format"] = kFormat;
    doc["version"] = kVersion;
    const time_t now = time(nullptr);
    doc["created"] = now > 1700000000 ? (long long)now : 0;
    doc["app"] = APP_NAME;
    doc["appVersion"] = APP_VERSION;
    doc["callsign"] = g_config.fullCallsign();
    doc["includesSecrets"] = includeSecrets;
    JsonObject cfg = doc["config"].to<JsonObject>();
    configToJson(g_config, cfg);
    if (includeSecrets) {
        cfg["wifi"]["password"] = g_config.wifiPassword;
        cfg["device"]["adminPassword"] = g_config.adminPassword;
    }
}

bool backupRestore(JsonObjectConst root, String &err) {
    const char *fmt = root["format"] | "";
    if (strcmp(fmt, kFormat) != 0) {
        err = "Not a platy-aprs backup file";
        return false;
    }
    if ((root["version"] | 0) > kVersion) {
        err = "Backup was made by a newer firmware";
        return false;
    }
    JsonObjectConst cfg = root["config"];
    if (!cfg) {
        err = "Backup has no config section";
        return false;
    }
    // Start from defaults so the result matches the backup rather than being
    // merged with the current settings; secrets that are absent are kept.
    AppConfig next;
    next.wifiPassword = g_config.wifiPassword;
    next.adminPassword = g_config.adminPassword;
    configFromJson(next, cfg);
    err = configValidate(next);
    if (err.length()) return false;
    g_config = next;
    if (!configSave()) {
        err = "Saving the configuration failed";
        return false;
    }
    return true;
}

bool backupNameValid(const String &name) {
    if (name.length() < 6 || name.length() > 64 || !name.endsWith(".json") || name[0] == '.') return false;
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

bool backupSaveToSd(bool includeSecrets, String &nameOut, String &err) {
    if (!sdRemount()) {
        err = "No SD card";
        return false;
    }
    if (!sdMakeDirs(kDir)) {
        err = "Could not create " + String(kDir);
        return false;
    }
    char name[48];
    const time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(name, sizeof(name), "platy-aprs-%Y%m%d-%H%M%S.json", &tm);
    } else {
        // Clock not set: number the files instead.
        for (int n = 1; n < 1000; n++) {
            snprintf(name, sizeof(name), "platy-aprs-backup-%03d.json", n);
            if (!sdFs().exists(String(kDir) + "/" + name)) break;
        }
    }
    JsonDocument doc;
    backupBuild(doc, includeSecrets);
    const String path = String(kDir) + "/" + name;
    File f = sdFs().open(path, FILE_WRITE);
    if (!f) {
        err = "Could not open " + path;
        return false;
    }
    const size_t n = serializeJsonPretty(doc, f);
    f.close();
    if (n == 0) {
        sdFs().remove(path);
        err = "Write failed";
        return false;
    }
    nameOut = name;
    Serial.printf("[backup] saved %s (%u bytes)\n", path.c_str(), (unsigned)n);
    return true;
}

std::vector<SdBackupInfo> backupListSd() {
    std::vector<SdBackupInfo> out;
    // Only a mounted card: the web page lists every few seconds, and a probe for
    // a missing card blocks. sdLoop() mounts cards in the background.
    if (!sdMounted()) return out;
    File dir = sdFs().open(kDir);
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        const int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (!f.isDirectory() && backupNameValid(n)) out.push_back({n, (size_t)f.size()});
        f.close();
    }
    std::sort(out.begin(), out.end(), [](const SdBackupInfo &a, const SdBackupInfo &b) { return a.name > b.name; });
    return out;
}

bool backupReadSd(const String &name, JsonDocument &doc, String &err) {
    if (!backupNameValid(name)) {
        err = "Invalid file name";
        return false;
    }
    if (!sdRemount()) {
        err = "No SD card";
        return false;
    }
    File f = sdFs().open(String(kDir) + "/" + name, FILE_READ);
    if (!f) {
        err = "File not found";
        return false;
    }
    const DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) {
        err = String("Invalid JSON: ") + e.c_str();
        return false;
    }
    return true;
}

bool backupDeleteSd(const String &name) {
    if (!backupNameValid(name) || !sdRemount()) return false;
    return sdFs().remove(String(kDir) + "/" + name);
}

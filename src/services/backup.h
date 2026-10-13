#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Configuration backup/restore, as a JSON document:
//   { "format": "platy-aprs-backup", "version": 1, "created": <epoch|0>,
//     "app": "...", "appVersion": "...", "includesSecrets": bool, "config": {...} }

void backupBuild(JsonDocument &doc, bool includeSecrets);
// Validates and applies a backup to g_config and saves it. Secrets missing
// from the backup keep their current values. The caller should reboot.
bool backupRestore(JsonObjectConst root, String &err);

// microSD copies under /platy/backups.
struct SdBackupInfo {
    String name;
    size_t size;
};
bool backupSaveToSd(bool includeSecrets, String &nameOut, String &err);
std::vector<SdBackupInfo> backupListSd();
bool backupReadSd(const String &name, JsonDocument &doc, String &err);
bool backupDeleteSd(const String &name);
bool backupNameValid(const String &name);

#pragma once
#include <Arduino.h>
#include <vector>

// Packet log files on the SD card: /rhino/logs/YYYY-MM-DD.log (UTC), one line
// per packet: "2026-10-12 21:54:03Z RF< SRC>DST,PATH:info". Loop task only.

void sdLogPacket(const char *tnc2, bool tx, bool viaIs);
void sdLogLoop();   // flushes buffered lines every few seconds

struct SdLogFile {
    String name;
    uint32_t size;
};
std::vector<SdLogFile> sdLogList();   // newest first
// Full path for a log file name, or "" if the name is not a valid log name.
String sdLogPath(const String &name);
bool sdLogDelete(const String &name);

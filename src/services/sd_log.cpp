#include "sd_log.h"
#include <algorithm>
#include <time.h>
#include "config/config.h"
#include "hal/sd_card.h"

namespace {

constexpr const char *kDir = "/platy/logs";
constexpr size_t kFlushBytes = 4096;
constexpr uint32_t kFlushMs = 5000;

String s_buf;
String s_bufFile;    // file the buffered lines belong to
uint32_t s_firstMs;  // when the oldest buffered line was added

String fileFor(time_t now, char *stamp, size_t stampLen) {
    if (now < 1700000000) {   // clock not set yet
        snprintf(stamp, stampLen, "up %lus", (unsigned long)(millis() / 1000));
        return "nodate.log";
    }
    struct tm t;
    gmtime_r(&now, &t);
    strftime(stamp, stampLen, "%Y-%m-%d %H:%M:%SZ", &t);
    char name[20];
    strftime(name, sizeof(name), "%Y-%m-%d.log", &t);
    return name;
}

void flush() {
    if (s_buf.isEmpty()) return;
    if (sdMounted() && sdMakeDirs(kDir)) {
        File f = sdFs().open(String(kDir) + "/" + s_bufFile, FILE_APPEND);
        if (f) {
            f.write((const uint8_t *)s_buf.c_str(), s_buf.length());
            f.close();
        }
    }
    s_buf = "";
}

}  // namespace

void sdLogPacket(const char *tnc2, bool tx, bool viaIs) {
    if (!g_config.sdLog || (viaIs && !tx && !g_config.sdLogIs)) return;
    char stamp[32];
    const String file = fileFor(time(nullptr), stamp, sizeof(stamp));
    if (file != s_bufFile) {
        flush();
        s_bufFile = file;
    }
    if (s_buf.isEmpty()) s_firstMs = millis();
    s_buf += stamp;
    s_buf += viaIs ? (tx ? " IS> " : " IS< ") : (tx ? " RF> " : " RF< ");
    s_buf += tnc2;
    s_buf += '\n';
    if (s_buf.length() >= kFlushBytes) flush();
}

void sdLogLoop() {
    if (s_buf.length() && millis() - s_firstMs >= kFlushMs) flush();
}

String sdLogPath(const String &name) {
    if (!name.endsWith(".log") || name.indexOf('/') >= 0 || name.indexOf("..") >= 0 || name.length() > 32) return "";
    return String(kDir) + "/" + name;
}

std::vector<SdLogFile> sdLogList() {
    flush();
    std::vector<SdLogFile> out;
    if (!sdMounted()) return out;
    File dir = sdFs().open(kDir);
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const String n = f.name();
        if (!f.isDirectory() && n.endsWith(".log")) out.push_back({n, (uint32_t)f.size()});
    }
    std::sort(out.begin(), out.end(), [](const SdLogFile &a, const SdLogFile &b) { return a.name > b.name; });
    return out;
}

bool sdLogDelete(const String &name) {
    const String p = sdLogPath(name);
    if (p.isEmpty() || !sdMounted()) return false;
    if (name == s_bufFile) s_buf = "";
    return sdFs().remove(p);
}

#include "data_store.h"
#include "sd_log.h"
#include "config/config.h"
#include <esp_heap_caps.h>
#include <time.h>

uint32_t g_dataGen = 1;

uint32_t uptimeS() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

static time_t nowEpoch() {
    const time_t t = time(nullptr);
    return t > 1700000000 ? t : 0;
}

static void *psAlloc(size_t bytes) {
    void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
    return p ? p : calloc(1, bytes);
}

static void copyStr(char *dst, size_t cap, const std::string &src) {
    const size_t n = std::min(src.size(), cap - 1);
    memcpy(dst, src.data(), n);
    dst[n] = 0;
}

static void copyPrintable(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    for (; src[i] && i < cap - 1; i++) {
        const unsigned char c = src[i];
        dst[i] = (c < 0x20 || c == 0x7F) ? '.' : c;
    }
    dst[i] = 0;
}

// ── Packet log ───────────────────────────────────────────────────────────────

static constexpr size_t kLogCap = 200;
static LogEntry *s_log;
static size_t s_logHead;   // next slot to write
static size_t s_logCount;
static uint32_t s_logSeq;

void packetLogBegin() { s_log = (LogEntry *)psAlloc(sizeof(LogEntry) * kLogCap); }

void packetLogAdd(const char *tnc2, bool tx, bool viaIs) {
    sdLogPacket(tnc2, tx, viaIs);
    if (!s_log) return;
    LogEntry &e = s_log[s_logHead];
    e.seq = ++s_logSeq;
    e.uptimeS = uptimeS();
    e.epoch = nowEpoch();
    e.tx = tx;
    e.viaIs = viaIs;
    copyPrintable(e.raw, sizeof(e.raw), tnc2);
    s_logHead = (s_logHead + 1) % kLogCap;
    if (s_logCount < kLogCap) s_logCount++;
    g_dataGen++;
}

uint32_t packetLogLastSeq() { return s_logSeq; }
size_t packetLogCount() { return s_logCount; }

const LogEntry *packetLogAt(size_t i) {
    if (i >= s_logCount) return nullptr;
    return &s_log[(s_logHead + kLogCap - 1 - i) % kLogCap];
}

// ── Station database ─────────────────────────────────────────────────────────

static constexpr size_t kStationCap = 300;
static Station *s_st;
// Indices into s_st, most recently heard first.
static uint16_t *s_order;
static size_t s_stCount;

void stationDbBegin() {
    s_st = (Station *)psAlloc(sizeof(Station) * kStationCap);
    s_order = (uint16_t *)psAlloc(sizeof(uint16_t) * kStationCap);
}

void stationDbClear() {
    s_stCount = 0;
    g_dataGen++;
}

size_t stationDbCount() { return s_stCount; }

const Station *stationDbAt(size_t i) { return i < s_stCount ? &s_st[s_order[i]] : nullptr; }

static int findIndex(const char *call) {
    for (size_t i = 0; i < s_stCount; i++) {
        if (strcmp(s_st[s_order[i]].call, call) == 0) return (int)i;
    }
    return -1;
}

const Station *stationDbFind(const char *call) {
    const int i = findIndex(call);
    return i < 0 ? nullptr : &s_st[s_order[i]];
}

void stationDbUpdate(const aprs::Packet &p, const char *path, const char *raw, bool viaIs) {
    if (!s_st) return;
    // Messages, acks, queries and telemetry carry nothing for the station list
    // apart from "heard"; record them so the list reflects activity.
    const bool isObj = p.type == aprs::Type::Object || p.type == aprs::Type::Item;
    const std::string &key = isObj ? p.objectName : p.source;
    if (key.empty()) return;

    // Strip trailing spaces from object names.
    char call[10];
    copyStr(call, sizeof(call), key);
    for (int k = strlen(call) - 1; k >= 0 && call[k] == ' '; k--) call[k] = 0;

    if (isObj && !p.objectAlive) {
        const int i = findIndex(call);
        if (i >= 0) {
            // The slot becomes free once nothing in s_order references it.
            memmove(&s_order[i], &s_order[i + 1], (s_stCount - i - 1) * sizeof(uint16_t));
            s_stCount--;
            g_dataGen++;
        }
        return;
    }

    int pos = findIndex(call);
    uint16_t slot;
    if (pos >= 0) {
        slot = s_order[pos];
        memmove(&s_order[1], &s_order[0], pos * sizeof(uint16_t));
    } else {
        if (s_stCount < kStationCap) {
            // Find a slot not referenced by s_order[0..count).
            static bool used[kStationCap];
            memset(used, 0, sizeof(used));
            for (size_t i = 0; i < s_stCount; i++) used[s_order[i]] = true;
            slot = 0;
            while (used[slot]) slot++;
            memmove(&s_order[1], &s_order[0], s_stCount * sizeof(uint16_t));
            s_stCount++;
        } else {
            slot = s_order[s_stCount - 1];   // evict the least recently heard
            memmove(&s_order[1], &s_order[0], (s_stCount - 1) * sizeof(uint16_t));
        }
        memset(&s_st[slot], 0, sizeof(Station));
        strcpy(s_st[slot].call, call);
        s_st[slot].firstHeardS = uptimeS();
    }
    s_order[0] = slot;

    Station &s = s_st[slot];
    s.isObject = isObj;
    if (isObj) copyStr(s.owner, sizeof(s.owner), p.source);
    s.lastHeardS = uptimeS();
    s.lastEpoch = nowEpoch();
    if (s.packets < 0xFFFF) s.packets++;
    s.viaIs = viaIs;
    if (!viaIs && !p.thirdParty) s.lastRfS = s.lastHeardS ? s.lastHeardS : 1;
    s.lastType = p.type;
    if (p.hasPos) {
        s.hasPos = true;
        s.lat = p.lat;
        s.lon = p.lon;
        s.symTable = p.symTable;
        s.symCode = p.symCode;
        s.hasCourseSpeed = p.hasCourseSpeed;
        s.course = p.course;
        s.speedKnots = p.speedKnots;
        s.hasAlt = p.hasAlt;
        s.altFeet = p.altFeet;
        if (!p.comment.empty() || p.type != aprs::Type::MicE) copyStr(s.comment, sizeof(s.comment), p.comment);
    }
    if (p.hasWeather) {
        s.hasWeather = true;
        if (p.hasTempF) { s.hasTempF = true; s.tempF = p.tempF; }
        if (p.hasWind) { s.hasWind = true; s.windDir = p.windDir; s.windMph = p.windMph; s.gustMph = p.gustMph; }
        if (p.hasHumidity) { s.hasHumidity = true; s.humidity = p.humidity; }
        if (p.hasPressure) { s.hasPressure = true; s.pressureMbar = p.pressureMbar; }
    }
    if (p.type == aprs::Type::Status) copyStr(s.status, sizeof(s.status), p.status);
    copyPrintable(s.path, sizeof(s.path), path ? path : "");
    copyPrintable(s.lastRaw, sizeof(s.lastRaw), raw ? raw : "");
    g_dataGen++;
}

// ── Last message ─────────────────────────────────────────────────────────────

static HeardMessage s_lastMsg;

void lastMessageUpdate(const aprs::Packet &p) {
    if (p.type != aprs::Type::Message || p.isAck || p.isRej) return;
    HeardMessage m{};
    m.valid = true;
    copyStr(m.from, sizeof(m.from), p.source);
    copyStr(m.to, sizeof(m.to), p.addressee);
    for (int k = strlen(m.to) - 1; k >= 0 && m.to[k] == ' '; k--) m.to[k] = 0;
    copyStr(m.text, sizeof(m.text), p.text);
    m.forMe = g_config.fullCallsign().equalsIgnoreCase(m.to);
    m.uptimeS = uptimeS();
    s_lastMsg = m;
    g_dataGen++;
}

const HeardMessage &lastMessage() { return s_lastMsg; }

#pragma once
#include <Arduino.h>
#include "aprs/aprs_parser.h"

// Everything here is touched only from the Arduino loop task (router, UI and
// web handlers), so no locking is needed.

// ── Packet log ───────────────────────────────────────────────────────────────

struct LogEntry {
    uint32_t seq;        // monotonically increasing, never 0
    uint32_t uptimeS;    // when received / sent
    time_t epoch;        // 0 if the clock was not set
    bool tx;
    bool viaIs;
    char raw[256];       // TNC2 text (non-printables replaced)
};

void packetLogBegin();
void packetLogAdd(const char *tnc2, bool tx, bool viaIs);
uint32_t packetLogLastSeq();
size_t packetLogCount();
// i = 0 is the newest entry.
const LogEntry *packetLogAt(size_t i);

// ── Station database ─────────────────────────────────────────────────────────

struct Station {
    char call[10];       // station callsign or object/item name
    bool isObject;
    char owner[10];      // for objects: the station that sent it
    uint32_t firstHeardS, lastHeardS;   // uptime seconds
    time_t lastEpoch;
    uint16_t packets;
    bool viaIs;
    aprs::Type lastType;

    bool hasPos;
    double lat, lon;
    char symTable, symCode;
    bool hasCourseSpeed;
    int16_t course;
    float speedKnots;
    bool hasAlt;
    float altFeet;

    bool hasWeather;
    bool hasTempF;
    int16_t tempF;
    bool hasWind;
    int16_t windDir, windMph, gustMph;
    bool hasHumidity;
    uint8_t humidity;
    bool hasPressure;
    float pressureMbar;

    char comment[64];
    char status[64];
    char path[64];       // digipeater path of the last packet heard
    char lastRaw[160];
};

void stationDbBegin();
void stationDbUpdate(const aprs::Packet &p, const char *path, const char *raw, bool viaIs);
size_t stationDbCount();
// Sorted most-recently-heard first.
const Station *stationDbAt(size_t i);
const Station *stationDbFind(const char *call);
void stationDbClear();

// ── Last received APRS message (text messages and bulletins, not acks) ──────

struct HeardMessage {
    bool valid;
    bool forMe;          // addressed to our full callsign
    char from[10];
    char to[10];
    char text[68];
    uint32_t uptimeS;
};

void lastMessageUpdate(const aprs::Packet &p);
const HeardMessage &lastMessage();

// Bumped whenever the log or the station list changes, so screens can skip
// redundant rebuilds.
extern uint32_t g_dataGen;

uint32_t uptimeS();

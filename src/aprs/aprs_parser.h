#pragma once
#include <cstdint>
#include <string>

// APRS info-field parser. Pure C++, host-testable.
namespace aprs {

enum class Type {
    Unknown,
    Position,
    MicE,
    Message,
    Status,
    Object,
    Item,
    Weather,
    Telemetry,
    Query,
};

struct Packet {
    Type type = Type::Unknown;
    std::string source;   // originating station (inner source for third-party)
    std::string dest;
    bool thirdParty = false;

    bool hasPos = false;
    double lat = 0, lon = 0;
    char symTable = '/';
    char symCode = ' ';
    bool messaging = false;   // '=' / '@' capability

    bool hasCourseSpeed = false;
    int course = 0;          // degrees
    float speedKnots = 0;
    bool hasAlt = false;
    float altFeet = 0;

    std::string comment;

    // Message / ack / rej
    std::string addressee;
    std::string text;
    std::string msgId;
    bool isAck = false;
    bool isRej = false;

    // Object / item
    std::string objectName;
    bool objectAlive = true;

    // Status
    std::string status;

    // Weather (only fields that were present are flagged)
    bool hasWeather = false;
    bool hasTempF = false;
    int tempF = 0;
    bool hasWind = false;
    int windDir = 0, windMph = 0, gustMph = 0;
    bool hasHumidity = false;
    int humidity = 0;
    bool hasPressure = false;
    float pressureMbar = 0;
};

// src/dest are the AX.25 addresses (dest is needed for Mic-E latitude).
bool parse(const std::string &src, const std::string &dest, const std::string &info, Packet &out);

const char *typeName(Type t);

}  // namespace aprs

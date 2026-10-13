#pragma once
#include <cstdint>
#include <string>

// APRS info-field encoders. Pure C++, host-testable.
namespace aprs {

struct PositionReport {
    double lat = 0, lon = 0;
    char symTable = '/', symCode = '>';
    bool messaging = true;          // '=' instead of '!'
    bool hasCourseSpeed = false;
    int course = 0;                 // degrees, 0..359 (0 is sent as 360)
    double speedKnots = 0;
    bool hasAlt = false;
    double altFeet = 0;
    std::string comment;
};

// "=DDMM.mmN/DDDMM.mmW>CSE/SPD/A=001234comment"
std::string encodePosition(const PositionReport &r);

// ":ADDRESSEE:text{id". Text is cleaned of the characters APRS reserves
// ('|', '~', '{') and cut to 67 chars. An empty id sends no '{'.
std::string encodeMessage(const std::string &addressee, const std::string &text, const std::string &msgId);
std::string encodeAck(const std::string &addressee, const std::string &msgId);
std::string encodeRej(const std::string &addressee, const std::string &msgId);

// Text as it will actually be sent (what encodeMessage keeps).
std::string cleanMessageText(const std::string &text);

}  // namespace aprs

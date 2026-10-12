#include "aprs_encoder.h"
#include <math.h>
#include <stdio.h>

namespace aprs {

static std::string coord(double v, bool isLat) {
    const char hemi = isLat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
    v = fabs(v);
    int deg = (int)v;
    // Round to hundredths of a minute, carrying into degrees.
    int hmin = (int)lround((v - deg) * 6000.0);
    if (hmin >= 6000) {
        hmin -= 6000;
        deg++;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), isLat ? "%02d%02d.%02d%c" : "%03d%02d.%02d%c", deg, hmin / 100, hmin % 100, hemi);
    return buf;
}

std::string encodePosition(const PositionReport &r) {
    std::string s;
    s += r.messaging ? '=' : '!';
    s += coord(r.lat, true);
    s += r.symTable;
    s += coord(r.lon, false);
    s += r.symCode;
    char buf[24];
    if (r.hasCourseSpeed) {
        int c = ((r.course % 360) + 360) % 360;
        if (c == 0) c = 360;
        long spd = lround(r.speedKnots);
        if (spd < 0) spd = 0;
        if (spd > 999) spd = 999;
        snprintf(buf, sizeof(buf), "%03d/%03ld", c, spd);
        s += buf;
    }
    if (r.hasAlt) {
        long a = lround(r.altFeet);
        if (a < -99999) a = -99999;
        if (a > 999999) a = 999999;
        snprintf(buf, sizeof(buf), a < 0 ? "/A=-%05ld" : "/A=%06ld", a < 0 ? -a : a);
        s += buf;
    }
    s += r.comment.substr(0, 43);
    return s;
}

std::string cleanMessageText(const std::string &text) {
    std::string t;
    for (char c : text) {
        if (c == '|' || c == '~' || c == '{') continue;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = ' ';
        t += c;
        if (t.size() == 67) break;
    }
    return t;
}

static std::string addr9(const std::string &a) {
    std::string s = a.substr(0, 9);
    s.resize(9, ' ');
    return ":" + s + ":";
}

std::string encodeMessage(const std::string &addressee, const std::string &text, const std::string &msgId) {
    std::string s = addr9(addressee) + cleanMessageText(text);
    if (!msgId.empty()) s += "{" + msgId.substr(0, 5);
    return s;
}

std::string encodeAck(const std::string &addressee, const std::string &msgId) {
    return addr9(addressee) + "ack" + msgId;
}

std::string encodeRej(const std::string &addressee, const std::string &msgId) {
    return addr9(addressee) + "rej" + msgId;
}

}  // namespace aprs

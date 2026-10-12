#include "aprs_parser.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace aprs {

namespace {

bool allDigits(const std::string &s, size_t pos, size_t n) {
    if (pos + n > s.size()) return false;
    for (size_t i = pos; i < pos + n; i++) {
        if (!isdigit((unsigned char)s[i])) return false;
    }
    return true;
}

int toInt(const std::string &s, size_t pos, size_t n) {
    int v = 0;
    for (size_t i = pos; i < pos + n; i++) v = v * 10 + (s[i] - '0');
    return v;
}

// Pull "/A=dddddd" (feet) out of a comment.
void extractAltitude(Packet &p) {
    const size_t a = p.comment.find("/A=");
    if (a == std::string::npos) return;
    size_t i = a + 3;
    const bool neg = i < p.comment.size() && p.comment[i] == '-';
    const size_t digitsStart = neg ? i + 1 : i;
    const size_t n = neg ? 5 : 6;
    if (!allDigits(p.comment, digitsStart, n)) return;
    p.hasAlt = true;
    p.altFeet = (float)toInt(p.comment, digitsStart, n) * (neg ? -1 : 1);
    p.comment.erase(a, 3 + 6);
}

// Uncompressed lat "DDMM.hhN". Spaces (ambiguity) count as zero.
bool parseLat(std::string s, double &lat) {
    if (s.size() != 8 || s[4] != '.') return false;
    for (char &c : s) if (c == ' ') c = '0';
    if (!allDigits(s, 0, 4) || !allDigits(s, 5, 2)) return false;
    const char h = (char)toupper((unsigned char)s[7]);
    if (h != 'N' && h != 'S') return false;
    lat = toInt(s, 0, 2) + (toInt(s, 2, 2) + toInt(s, 5, 2) / 100.0) / 60.0;
    if (lat > 90) return false;
    if (h == 'S') lat = -lat;
    return true;
}

bool parseLon(std::string s, double &lon) {
    if (s.size() != 9 || s[5] != '.') return false;
    for (char &c : s) if (c == ' ') c = '0';
    if (!allDigits(s, 0, 5) || !allDigits(s, 6, 2)) return false;
    const char h = (char)toupper((unsigned char)s[8]);
    if (h != 'E' && h != 'W') return false;
    lon = toInt(s, 0, 3) + (toInt(s, 3, 2) + toInt(s, 6, 2) / 100.0) / 60.0;
    if (lon > 180) return false;
    if (h == 'W') lon = -lon;
    return true;
}

bool isBase91(char c) { return c >= '!' && c <= '{'; }

void parseWeatherFields(Packet &p, const std::string &s, size_t pos) {
    p.hasWeather = true;
    p.type = Type::Weather;
    // Fields: c(dir) s(speed) g t r p P h b — each a letter + fixed-width value.
    auto field = [&](char key, size_t width, int &out, bool allowNeg) -> bool {
        const size_t k = s.find(key, pos);
        if (k == std::string::npos || k + 1 + width > s.size()) return false;
        std::string v = s.substr(k + 1, width);
        if (v.find('.') != std::string::npos || v.find(' ') != std::string::npos) return false;
        const bool neg = allowNeg && v[0] == '-';
        if (!allDigits(v, neg ? 1 : 0, neg ? width - 1 : width)) return false;
        out = atoi(v.c_str());
        return true;
    };
    int v;
    if (field('g', 3, v, false)) p.gustMph = v;
    if (field('t', 3, v, true)) {
        p.hasTempF = true;
        p.tempF = v;
    }
    if (field('h', 2, v, false)) {
        p.hasHumidity = true;
        p.humidity = v == 0 ? 100 : v;
    }
    if (field('b', 5, v, false)) {
        p.hasPressure = true;
        p.pressureMbar = v / 10.0f;
    }
    if (!p.hasWind) {
        int d, sp;
        if (field('c', 3, d, false) && field('s', 3, sp, false)) {
            p.hasWind = true;
            p.windDir = d;
            p.windMph = sp;
        }
    }
}

// Position body starting at s[pos]: uncompressed or compressed, followed by
// optional data extension and comment.
bool parsePositionBody(Packet &p, const std::string &s, size_t pos) {
    if (pos >= s.size()) return false;
    const char first = s[pos];
    if (isdigit((unsigned char)first) || first == ' ') {
        if (pos + 19 > s.size()) return false;
        if (!parseLat(s.substr(pos, 8), p.lat)) return false;
        p.symTable = s[pos + 8];
        if (!parseLon(s.substr(pos + 9, 9), p.lon)) return false;
        p.symCode = s[pos + 18];
        p.hasPos = true;
        size_t rest = pos + 19;
        // CSE/SPD (or wind dir/speed for weather) "ddd/sss"
        if (rest + 7 <= s.size() && s[rest + 3] == '/' && allDigits(s, rest, 3) && allDigits(s, rest + 4, 3)) {
            const int a = toInt(s, rest, 3), b = toInt(s, rest + 4, 3);
            if (p.symCode == '_') {
                p.hasWind = true;
                p.windDir = a;
                p.windMph = b;
            } else {
                p.hasCourseSpeed = true;
                p.course = a == 360 ? 0 : a;
                p.speedKnots = (float)b;
            }
            rest += 7;
        }
        if (p.symCode == '_') {
            parseWeatherFields(p, s, rest);
            p.comment.clear();
        } else {
            p.comment = s.substr(rest);
        }
    } else {
        // Compressed: table, YYYY, XXXX, code, cs, T
        if (pos + 13 > s.size()) return false;
        for (int i = 1; i <= 8; i++) {
            if (!isBase91(s[pos + i])) return false;
        }
        char table = s[pos];
        if (table >= 'a' && table <= 'j') table = (char)('0' + (table - 'a'));
        p.symTable = table;
        auto b91 = [&](size_t at) {
            long v = 0;
            for (int i = 0; i < 4; i++) v = v * 91 + (s[at + i] - 33);
            return v;
        };
        p.lat = 90.0 - b91(pos + 1) / 380926.0;
        p.lon = -180.0 + b91(pos + 5) / 190463.0;
        p.symCode = s[pos + 9];
        p.hasPos = true;
        const char c = s[pos + 10], sp = s[pos + 11], t = s[pos + 12];
        if (c != ' ') {
            const int tv = t - 33;
            if (((tv >> 3) & 0x03) == 2) {
                p.hasAlt = true;
                p.altFeet = (float)pow(1.002, (c - 33) * 91 + (sp - 33));
            } else if (c >= '!' && c <= 'z') {
                p.hasCourseSpeed = true;
                p.course = (c - 33) * 4;
                p.speedKnots = (float)(pow(1.08, sp - 33) - 1.0);
            }
        }
        if (p.symCode == '_') {
            parseWeatherFields(p, s, pos + 13);
        } else {
            p.comment = s.substr(pos + 13);
        }
    }
    if (p.type != Type::Weather) extractAltitude(p);
    return true;
}

bool parseMicE(Packet &p, const std::string &dest, const std::string &s) {
    if (dest.size() < 6 || s.size() < 9) return false;
    int digits[6];
    for (int i = 0; i < 6; i++) {
        const char c = dest[i];
        if (c >= '0' && c <= '9') digits[i] = c - '0';
        else if (c >= 'A' && c <= 'J') digits[i] = c - 'A';
        else if (c >= 'P' && c <= 'Y') digits[i] = c - 'P';
        else if (c == 'K' || c == 'L' || c == 'Z') digits[i] = 0;
        else return false;
    }
    double lat = digits[0] * 10 + digits[1] + (digits[2] * 10 + digits[3] + (digits[4] * 10 + digits[5]) / 100.0) / 60.0;
    const bool north = dest[3] >= 'P' && dest[3] <= 'Z';
    const bool lonOffset = dest[4] >= 'P' && dest[4] <= 'Z';
    const bool west = dest[5] >= 'P' && dest[5] <= 'Z';

    int d = (uint8_t)s[1] - 28;
    if (lonOffset) d += 100;
    if (d >= 180 && d <= 189) d -= 80;
    else if (d >= 190 && d <= 199) d -= 190;
    int m = (uint8_t)s[2] - 28;
    if (m >= 60) m -= 60;
    const int h = (uint8_t)s[3] - 28;
    double lon = d + (m + h / 100.0) / 60.0;
    if (lat > 90 || lon > 180 || h < 0 || h > 99) return false;

    const int sp = (uint8_t)s[4] - 28, dc = (uint8_t)s[5] - 28, se = (uint8_t)s[6] - 28;
    int speed = sp * 10 + dc / 10;
    if (speed >= 800) speed -= 800;
    int course = (dc % 10) * 100 + se;
    if (course >= 400) course -= 400;

    p.type = Type::MicE;
    p.lat = north ? lat : -lat;
    p.lon = west ? -lon : lon;
    p.hasPos = true;
    p.symCode = s[7];
    p.symTable = s[8];
    p.hasCourseSpeed = true;
    p.speedKnots = (float)speed;
    p.course = course;

    std::string c = s.substr(9);
    // Optional altitude: 3 base-91 chars + '}' (metres + 10000), possibly
    // after a one-byte radio-type prefix.
    for (size_t off = 0; off <= 1 && off + 4 <= c.size(); off++) {
        if (c[off + 3] == '}' && isBase91(c[off]) && isBase91(c[off + 1]) && isBase91(c[off + 2])) {
            const int metres = (c[off] - 33) * 91 * 91 + (c[off + 1] - 33) * 91 + (c[off + 2] - 33) - 10000;
            p.hasAlt = true;
            p.altFeet = metres * 3.28084f;
            c.erase(0, off + 4);
            break;
        }
    }
    if (!c.empty() && (c[0] == '>' || c[0] == ']' || c[0] == '`' || c[0] == '\'')) c.erase(0, 1);
    p.comment = c;
    return true;
}

bool parseMessage(Packet &p, const std::string &s) {
    if (s.size() < 11 || s[10] != ':') return false;
    p.type = Type::Message;
    std::string addr = s.substr(1, 9);
    while (!addr.empty() && addr.back() == ' ') addr.pop_back();
    p.addressee = addr;
    std::string body = s.substr(11);
    if ((body.compare(0, 3, "ack") == 0 || body.compare(0, 3, "rej") == 0) && body.size() > 3 &&
        body.size() <= 3 + 5 + 4) {
        const bool ack = body[0] == 'a';
        std::string id = body.substr(3);
        // reply-ack form "ackMM}AA": the id being acknowledged is before '}'.
        const size_t brace = id.find('}');
        if (brace != std::string::npos) id = id.substr(0, brace);
        p.isAck = ack;
        p.isRej = !ack;
        p.msgId = id;
        return true;
    }
    const size_t brace = body.rfind('{');
    if (brace != std::string::npos && body.size() - brace - 1 <= 5 + 3 && body.size() - brace > 1) {
        std::string id = body.substr(brace + 1);
        const size_t close = id.find('}');
        if (close != std::string::npos) id = id.substr(0, close);
        p.msgId = id;
        body = body.substr(0, brace);
    }
    p.text = body;
    if (p.addressee.compare(0, 3, "BLN") == 0) p.type = Type::Message;
    if (body.compare(0, 5, "PARM.") == 0 || body.compare(0, 5, "UNIT.") == 0 ||
        body.compare(0, 5, "EQNS.") == 0 || body.compare(0, 5, "BITS.") == 0)
        p.type = Type::Telemetry;
    return true;
}

bool parseInner(const std::string &src, const std::string &dest, const std::string &s, Packet &p, int depth);

bool parseThirdParty(const std::string &s, Packet &p, int depth) {
    const std::string inner = s.substr(1);
    const size_t gt = inner.find('>');
    const size_t colon = inner.find(':');
    if (gt == std::string::npos || colon == std::string::npos || gt > colon) return false;
    const std::string src = inner.substr(0, gt);
    std::string header = inner.substr(gt + 1, colon - gt - 1);
    const size_t comma = header.find(',');
    const std::string dst = header.substr(0, comma);
    p.thirdParty = true;
    return parseInner(src, dst, inner.substr(colon + 1), p, depth + 1);
}

bool parseInner(const std::string &src, const std::string &dest, const std::string &s, Packet &p, int depth) {
    p.source = src;
    p.dest = dest;
    if (s.empty() || depth > 2) return false;
    const char dti = s[0];
    switch (dti) {
        case '!':
        case '=':
            p.type = Type::Position;
            p.messaging = dti == '=';
            // "!" may be preceded by up to 40 chars of junk; we only accept it at 0.
            return parsePositionBody(p, s, 1);
        case '/':
        case '@':
            p.type = Type::Position;
            p.messaging = dti == '@';
            if (s.size() < 8) return false;
            return parsePositionBody(p, s, 8);
        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            return parseMicE(p, dest, s);
        case ':':
            return parseMessage(p, s);
        case '>':
            p.type = Type::Status;
            p.status = s.substr(1);
            return true;
        case ';': {
            if (s.size() < 18) return false;
            p.type = Type::Object;
            std::string name = s.substr(1, 9);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            p.objectName = name;
            p.objectAlive = s[10] == '*';
            const bool ok = parsePositionBody(p, s, 18);
            if (p.type == Type::Weather) return ok;
            p.type = Type::Object;
            return ok;
        }
        case ')': {
            size_t end = 1;
            while (end < s.size() && end <= 10 && s[end] != '!' && s[end] != '_') end++;
            if (end >= s.size() || end < 4) return false;
            p.type = Type::Item;
            p.objectName = s.substr(1, end - 1);
            p.objectAlive = s[end] == '!';
            const bool ok = parsePositionBody(p, s, end + 1);
            p.type = Type::Item;
            return ok;
        }
        case '_':
            if (s.size() < 9) return false;
            parseWeatherFields(p, s, 9);
            return true;
        case 'T':
            p.type = Type::Telemetry;
            p.comment = s;
            return true;
        case '?':
            p.type = Type::Query;
            p.comment = s;
            return true;
        case '}':
            return parseThirdParty(s, p, depth);
        default:
            p.type = Type::Unknown;
            p.comment = s;
            return false;
    }
}

}  // namespace

bool parse(const std::string &src, const std::string &dest, const std::string &info, Packet &out) {
    out = Packet();
    return parseInner(src, dest, info, out, 0);
}

const char *typeName(Type t) {
    switch (t) {
        case Type::Position: return "position";
        case Type::MicE: return "mic-e";
        case Type::Message: return "message";
        case Type::Status: return "status";
        case Type::Object: return "object";
        case Type::Item: return "item";
        case Type::Weather: return "weather";
        case Type::Telemetry: return "telemetry";
        case Type::Query: return "query";
        default: return "unknown";
    }
}

}  // namespace aprs

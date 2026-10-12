#include "ax25.h"
#include <ctype.h>

namespace ax25 {

std::string Address::str() const {
    if (ssid == 0) return call;
    return call + "-" + std::to_string(ssid);
}

bool parseAddress(const std::string &textIn, Address &out) {
    std::string text = textIn;
    out = Address();
    if (!text.empty() && text.back() == '*') {
        out.repeated = true;
        text.pop_back();
    }
    const size_t dash = text.find('-');
    std::string call = dash == std::string::npos ? text : text.substr(0, dash);
    if (call.empty() || call.size() > 6) return false;
    for (char &c : call) {
        if (!isalnum((unsigned char)c)) return false;
        c = (char)toupper((unsigned char)c);
    }
    out.call = call;
    if (dash != std::string::npos) {
        const std::string s = text.substr(dash + 1);
        if (s.empty() || s.size() > 2) return false;
        int v = 0;
        for (char c : s) {
            if (!isdigit((unsigned char)c)) return false;
            v = v * 10 + (c - '0');
        }
        if (v > 15) return false;
        out.ssid = (uint8_t)v;
    }
    return true;
}

static bool decodeAddr(const uint8_t *p, Address &a, bool &last) {
    std::string call;
    for (int i = 0; i < 6; i++) {
        const char c = (char)(p[i] >> 1);
        if (c == ' ') continue;
        if (!isalnum((unsigned char)c)) return false;
        call.push_back(c);
    }
    if (call.empty()) return false;
    a.call = call;
    a.ssid = (p[6] >> 1) & 0x0F;
    a.repeated = (p[6] & 0x80) != 0;
    last = (p[6] & 0x01) != 0;
    return true;
}

bool decode(const uint8_t *data, size_t len, Frame &out) {
    out = Frame();
    if (len < 16) return false;
    size_t pos = 0;
    bool last = false;
    if (!decodeAddr(data, out.dst, last) || last) return false;
    out.dst.repeated = false;
    pos += 7;
    if (!decodeAddr(data + pos, out.src, last)) return false;
    out.src.repeated = false;
    pos += 7;
    while (!last) {
        if (pos + 7 > len || out.digis.size() == 8) return false;
        Address d;
        if (!decodeAddr(data + pos, d, last)) return false;
        out.digis.push_back(d);
        pos += 7;
    }
    if (pos + 2 > len) return false;
    out.control = data[pos++];
    out.pid = data[pos++];
    if (out.control != 0x03) return false;   // only UI frames carry APRS
    out.info.assign((const char *)data + pos, len - pos);
    return true;
}

static void encodeAddr(std::vector<uint8_t> &v, const Address &a, bool hBit, bool last) {
    for (int i = 0; i < 6; i++) {
        const char c = i < (int)a.call.size() ? a.call[i] : ' ';
        v.push_back((uint8_t)(c << 1));
    }
    uint8_t s = 0x60 | ((a.ssid & 0x0F) << 1);
    if (hBit) s |= 0x80;
    if (last) s |= 0x01;
    v.push_back(s);
}

std::vector<uint8_t> encode(const Frame &f) {
    std::vector<uint8_t> v;
    v.reserve(16 + 7 * f.digis.size() + f.info.size());
    // Command frame: C bit set on destination, clear on source (AX.25 v2).
    encodeAddr(v, f.dst, true, false);
    encodeAddr(v, f.src, false, f.digis.empty());
    for (size_t i = 0; i < f.digis.size(); i++) {
        encodeAddr(v, f.digis[i], f.digis[i].repeated, i + 1 == f.digis.size());
    }
    v.push_back(f.control);
    v.push_back(f.pid);
    v.insert(v.end(), f.info.begin(), f.info.end());
    return v;
}

std::string toTnc2(const Frame &f) {
    std::string s = f.src.str() + ">" + f.dst.str();
    int lastRepeated = -1;
    for (size_t i = 0; i < f.digis.size(); i++) {
        if (f.digis[i].repeated) lastRepeated = (int)i;
    }
    for (size_t i = 0; i < f.digis.size(); i++) {
        s += "," + f.digis[i].str();
        if ((int)i == lastRepeated) s += "*";
    }
    s += ":";
    s += f.info;
    return s;
}

bool parsePath(const std::string &path, std::vector<Address> &out) {
    out.clear();
    size_t start = 0;
    while (start < path.size()) {
        size_t comma = path.find(',', start);
        if (comma == std::string::npos) comma = path.size();
        const std::string item = path.substr(start, comma - start);
        if (!item.empty()) {
            Address a;
            if (!parseAddress(item, a) || out.size() == 8) return false;
            out.push_back(a);
        }
        start = comma + 1;
    }
    return true;
}

bool fromTnc2(const std::string &line, Frame &out) {
    out = Frame();
    const size_t colon = line.find(':');
    const size_t gt = line.find('>');
    if (colon == std::string::npos || gt == std::string::npos || gt > colon) return false;
    if (!parseAddress(line.substr(0, gt), out.src)) return false;
    const std::string header = line.substr(gt + 1, colon - gt - 1);
    const size_t comma = header.find(',');
    if (!parseAddress(header.substr(0, comma), out.dst)) return false;
    if (comma != std::string::npos) {
        if (!parsePath(header.substr(comma + 1), out.digis)) return false;
        // A '*' marks every digipeater up to and including it as repeated.
        int lastStar = -1;
        for (size_t i = 0; i < out.digis.size(); i++) {
            if (out.digis[i].repeated) lastStar = (int)i;
        }
        for (int i = 0; i <= lastStar; i++) out.digis[i].repeated = true;
    }
    out.src.repeated = out.dst.repeated = false;
    out.info = line.substr(colon + 1);
    return true;
}

}  // namespace ax25

#include "gating.h"

namespace gating {

namespace {

std::string trimInfo(const std::string &info) {
    const size_t end = info.find_first_of(std::string("\r\n\0", 3));
    return end == std::string::npos ? info : info.substr(0, end);
}

}  // namespace

bool splitIsLine(const std::string &line, IsPacket &out) {
    if (line.empty() || line[0] == '#') return false;
    const size_t gt = line.find('>');
    const size_t colon = line.find(':');
    if (gt == std::string::npos || colon == std::string::npos || gt == 0 || gt > colon) return false;
    out.src = line.substr(0, gt);
    const std::string header = line.substr(gt + 1, colon - gt - 1);
    const size_t comma = header.find(',');
    out.dst = header.substr(0, comma);
    out.path = comma == std::string::npos ? "" : header.substr(comma + 1);
    out.info = trimInfo(line.substr(colon + 1));
    return !out.dst.empty();
}

bool pathBlocksGating(const std::string &path) {
    size_t start = 0;
    while (start <= path.size()) {
        size_t comma = path.find(',', start);
        if (comma == std::string::npos) comma = path.size();
        std::string item = path.substr(start, comma - start);
        if (!item.empty() && item.back() == '*') item.pop_back();
        if (item == "TCPIP" || item == "TCPXX" || item == "NOGATE" || item == "RFONLY") return true;
        start = comma + 1;
    }
    return false;
}

bool rfToIs(const std::string &tnc2, const std::string &igateCall, std::string &out) {
    IsPacket p;
    if (!splitIsLine(tnc2, p) || pathBlocksGating(p.path)) return false;
    if (p.info.empty() || p.info[0] == '?') return false;
    if (p.info[0] == '}') {
        // Third-party: gate the inner packet if it is itself gateable.
        IsPacket inner;
        if (!splitIsLine(p.info.substr(1), inner) || pathBlocksGating(inner.path)) return false;
        if (inner.info.empty() || inner.info[0] == '?') return false;
        p = inner;
    }
    out = p.src + ">" + p.dst + (p.path.empty() ? "" : "," + p.path) + ",qAR," + igateCall + ":" + p.info;
    return true;
}

std::string isToRfInfo(const IsPacket &p, const std::string &igateCall) {
    return "}" + p.src + ">" + p.dst + ",TCPIP," + igateCall + "*:" + p.info;
}

bool digipeat(ax25::Frame &f, const ax25::Address &me) {
    if (f.src == me) return false;
    size_t i = 0;
    while (i < f.digis.size() && f.digis[i].repeated) i++;
    if (i == f.digis.size()) return false;
    ax25::Address &next = f.digis[i];
    if (next == me) {
        next.repeated = true;
        return true;
    }
    if (next.call != "WIDE1" || next.ssid != 1) return false;
    ax25::Address mine = me;
    mine.repeated = true;
    if (f.digis.size() >= 8) {
        next = mine;   // no room to insert: plain substitution
        return true;
    }
    next.ssid = 0;
    next.repeated = true;
    f.digis.insert(f.digis.begin() + i, mine);
    return true;
}

}  // namespace gating

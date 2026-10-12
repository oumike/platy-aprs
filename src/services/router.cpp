#include "router.h"
#include "aprs/aprs_parser.h"
#include "aprs/gating.h"
#include "ax25/ax25.h"
#include "config/config.h"
#include "data_store.h"
#include "hal/gps.h"
#include "messages.h"
#include "net/aprs_is.h"
#include "version.h"
#include "transport/ble_kiss.h"

namespace {

constexpr uint32_t kDupeMs = 30000;
constexpr uint32_t kRfHeardS = 30 * 60;   // "heard on RF recently" for IS->RF gating

// Recently digipeated / gated packets (hash of src, dst and info).
struct Seen {
    uint32_t hash, ms;
};
Seen s_seen[32];
size_t s_seenNext;

uint32_t fnv(const std::string &a, const std::string &b, const std::string &c) {
    uint32_t h = 2166136261u;
    for (const std::string *s : {&a, &b, &c}) {
        for (unsigned char ch : *s) h = (h ^ ch) * 16777619u;
        h = (h ^ 0xFF) * 16777619u;
    }
    return h;
}

// True if seen within kDupeMs; otherwise remembers it.
bool dupe(uint32_t h) {
    const uint32_t now = millis();
    for (const Seen &s : s_seen)
        if (s.hash == h && s.ms && now - s.ms < kDupeMs) return true;
    s_seen[s_seenNext] = {h, now ? now : 1};
    s_seenNext = (s_seenNext + 1) % (sizeof(s_seen) / sizeof(s_seen[0]));
    return false;
}

String myCall() {
    String c = g_config.fullCallsign();
    c.toUpperCase();
    return c;
}

// Digipeater path as printed in TNC2 ("WIDE1-1*,WIDE2-1").
std::string pathOf(const std::string &tnc2) {
    const size_t gt = tnc2.find('>'), colon = tnc2.find(':');
    const size_t comma = tnc2.find(',', gt);
    if (comma == std::string::npos || comma > colon) return "";
    return tnc2.substr(comma + 1, colon - comma - 1);
}

bool sendFrame(const ax25::Frame &f) {
    const std::vector<uint8_t> bin = ax25::encode(f);
    if (bin.empty() || !g_ble.sendAx25(bin.data(), bin.size())) return false;
    const std::string tnc2 = ax25::toTnc2(f);
    packetLogAdd(tnc2.c_str(), true, false);
    Serial.printf("[tx] %s\n", tnc2.c_str());
    return true;
}

void digipeat(const ax25::Frame &rx) {
    if (!g_config.digiEnabled || !g_ble.connected()) return;
    ax25::Address me;
    if (!ax25::parseAddress(myCall().c_str(), me)) return;
    ax25::Frame f = rx;
    if (!gating::digipeat(f, me)) return;
    if (dupe(fnv(f.src.str(), f.dst.str(), f.info))) return;
    sendFrame(f);
}

void gateToIs(const std::string &tnc2) {
    if (!g_config.igateRfToIs || !aprsIsReady()) return;
    std::string line;
    if (!gating::rfToIs(tnc2, myCall().c_str(), line)) return;
    if (aprsIsSend(line)) aprsIsCountGated(true);
}

bool heardOnRfRecently(const std::string &call) {
    String c(call.c_str());
    c.trim();
    const Station *s = stationDbFind(c.c_str());
    return s && s->lastRfS && uptimeS() - s->lastRfS < kRfHeardS;
}

// Messages from APRS-IS to a station that is local on RF (and whose sender is
// not) are retransmitted as third-party traffic.
void gateToRf(const gating::IsPacket &ip, const aprs::Packet &p) {
    if (!g_config.igateIsToRfMsgs || !g_ble.connected() || p.type != aprs::Type::Message) return;
    String to(p.addressee.c_str());
    to.trim();
    if (to.equalsIgnoreCase(myCall()) || !heardOnRfRecently(p.addressee) || heardOnRfRecently(ip.src)) return;
    if (dupe(fnv(ip.src, ip.dst, ip.info))) return;
    ax25::Frame f;
    if (!ax25::parseAddress(myCall().c_str(), f.src) || !ax25::parseAddress(APRS_TOCALL, f.dst) ||
        !ax25::parsePath(g_config.path.c_str(), f.digis))
        return;
    f.info = gating::isToRfInfo(ip, myCall().c_str());
    if (sendFrame(f)) aprsIsCountGated(false);
}

void handleRf(const RxFrame &rx) {
    ax25::Frame f;
    if (!ax25::decode(rx.data, rx.len, f)) {
        Serial.printf("[rx] undecodable frame (%u bytes)\n", rx.len);
        return;
    }
    const std::string tnc2 = ax25::toTnc2(f);
    packetLogAdd(tnc2.c_str(), false, false);
    Serial.printf("[rx] %s\n", tnc2.c_str());
    gateToIs(tnc2);
    digipeat(f);

    aprs::Packet p;
    if (!aprs::parse(f.src.str(), f.dst.str(), f.info, p)) return;
    stationDbUpdate(p, pathOf(tnc2).c_str(), tnc2.c_str(), false);
    lastMessageUpdate(p);
    messagesOnRx(p);
}

}  // namespace

void routerBegin() {
    packetLogBegin();
    stationDbBegin();
}

void routerLoop() {
    RxFrame rx;
    // Bounded so a burst cannot starve the UI.
    for (int i = 0; i < 4 && g_ble.receive(rx); i++) handleRf(rx);
}

void routerOnIsLine(const std::string &line) {
    gating::IsPacket ip;
    if (!gating::splitIsLine(line, ip)) return;
    packetLogAdd(line.c_str(), false, true);
    aprs::Packet p;
    if (!aprs::parse(ip.src, ip.dst, ip.info, p)) return;
    stationDbUpdate(p, ip.path.c_str(), line.c_str(), true);
    lastMessageUpdate(p);
    messagesOnRx(p);
    gateToRf(ip, p);
}

bool myPosition(double &lat, double &lon) {
    const GpsFix f = gpsFix();
    if (f.valid) {
        lat = f.lat;
        lon = f.lon;
        return true;
    }
    if (g_config.fixedLat == 0 && g_config.fixedLon == 0) return false;
    lat = g_config.fixedLat;
    lon = g_config.fixedLon;
    return true;
}

bool routerCanSend() { return g_ble.connected() || aprsIsReady(); }

bool routerSendInfo(const std::string &info, String &err) {
    if (g_config.callsign.isEmpty() || g_config.callsign.equalsIgnoreCase("N0CALL")) {
        err = "Set your callsign first";
        return false;
    }
    const bool rf = g_ble.connected(), is = aprsIsReady();
    if (!rf && !is) {
        err = g_config.aprsIsEnabled ? "Neither TNC nor APRS-IS connected" : "TNC not connected";
        return false;
    }
    ax25::Frame f;
    if (!ax25::parseAddress(myCall().c_str(), f.src) || !ax25::parseAddress(APRS_TOCALL, f.dst) ||
        !ax25::parsePath(g_config.path.c_str(), f.digis)) {
        err = "Invalid callsign or path";
        return false;
    }
    f.info = info;
    bool sent = false;
    if (rf) sent = sendFrame(f);
    if (is) {
        const std::string line = f.src.str() + ">" + f.dst.str() + ",TCPIP*:" + info;
        if (aprsIsSend(line)) {
            packetLogAdd(line.c_str(), true, true);
            Serial.printf("[tx-is] %s\n", line.c_str());
            sent = true;
        }
    }
    if (!sent) err = "Send queue full";
    return sent;
}

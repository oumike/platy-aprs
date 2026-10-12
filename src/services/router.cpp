#include "router.h"
#include "aprs/aprs_parser.h"
#include "ax25/ax25.h"
#include "config/config.h"
#include "data_store.h"
#include "hal/gps.h"
#include "messages.h"
#include "version.h"
#include "transport/ble_kiss.h"

void routerBegin() {
    packetLogBegin();
    stationDbBegin();
}

// Digipeater path as printed in TNC2 ("WIDE1-1*,WIDE2-1").
static std::string pathOf(const std::string &tnc2) {
    const size_t gt = tnc2.find('>'), colon = tnc2.find(':');
    const size_t comma = tnc2.find(',', gt);
    if (comma == std::string::npos || comma > colon) return "";
    return tnc2.substr(comma + 1, colon - comma - 1);
}

static void handleRf(const RxFrame &rx) {
    ax25::Frame f;
    if (!ax25::decode(rx.data, rx.len, f)) {
        Serial.printf("[rx] undecodable frame (%u bytes)\n", rx.len);
        return;
    }
    const std::string tnc2 = ax25::toTnc2(f);
    packetLogAdd(tnc2.c_str(), false, false);
    Serial.printf("[rx] %s\n", tnc2.c_str());

    aprs::Packet p;
    if (!aprs::parse(f.src.str(), f.dst.str(), f.info, p)) return;
    stationDbUpdate(p, pathOf(tnc2).c_str(), tnc2.c_str(), false);
    lastMessageUpdate(p);
    messagesOnRx(p);
}

void routerLoop() {
    RxFrame rx;
    // Bounded so a burst cannot starve the UI.
    for (int i = 0; i < 4 && g_ble.receive(rx); i++) handleRf(rx);
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

bool routerSendInfo(const std::string &info, String &err) {
    if (g_config.callsign.isEmpty() || g_config.callsign.equalsIgnoreCase("N0CALL")) {
        err = "Set your callsign first";
        return false;
    }
    if (!g_ble.connected()) {
        err = "TNC not connected";
        return false;
    }
    ax25::Frame f;
    if (!ax25::parseAddress(g_config.fullCallsign().c_str(), f.src) || !ax25::parseAddress(APRS_TOCALL, f.dst) ||
        !ax25::parsePath(g_config.path.c_str(), f.digis)) {
        err = "Invalid callsign or path";
        return false;
    }
    f.info = info;
    const std::vector<uint8_t> bin = ax25::encode(f);
    if (bin.empty() || !g_ble.sendAx25(bin.data(), bin.size())) {
        err = "TNC send queue full";
        return false;
    }
    const std::string tnc2 = ax25::toTnc2(f);
    packetLogAdd(tnc2.c_str(), true, false);
    Serial.printf("[tx] %s\n", tnc2.c_str());
    return true;
}

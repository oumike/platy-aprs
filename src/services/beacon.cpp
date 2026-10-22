#include "beacon.h"
#include "alerts.h"
#include "aprs/aprs_encoder.h"
#include "aprs/smart_beacon.h"
#include "config/config.h"
#include "data_store.h"
#include "hal/gps.h"
#include "router.h"
#include "transport/ble_kiss.h"

namespace {

aprs::SmartBeacon s_sb;   // tracks the last beacon for every mode
String s_lastError;
String s_waiting;
uint32_t s_lastCheckMs;
// No beacon of any kind (automatic, corner or "send now") within this of the last.
constexpr uint32_t kMinBeaconGapS = 30;

uint32_t holdoffS() {
    if (!s_sb.hasSent()) return 0;
    const uint32_t ago = uptimeS() - s_sb.lastS();
    return ago >= kMinBeaconGapS ? 0 : kMinBeaconGapS - ago;
}

aprs::SmartBeaconParams params() {
    aprs::SmartBeaconParams p;
    const SmartBeaconConfig &c = g_config.smart;
    p.slowRateS = c.slowRateS;
    p.fastRateS = c.fastRateS;
    p.slowSpeedKmh = c.slowSpeedKmh;
    p.fastSpeedKmh = c.fastSpeedKmh;
    p.minTurnDeg = c.minTurnDeg;
    p.turnSlope = c.turnSlope;
    p.minTurnTimeS = c.minTurnTimeS;
    return p;
}

bool fixedSet() { return g_config.fixedLat != 0 || g_config.fixedLon != 0; }

// GPS mode falls back to the fixed position when the receiver is turned off or
// not sending anything (no module fitted). A short grace after boot lets it start.
bool gpsFallback(const GpsFix &f) {
    if (g_config.beaconMode != BeaconMode::Gps || !fixedSet()) return false;
    return !f.enabled || (!f.alive && millis() > 15000);
}

bool useSmart() { return g_config.beaconMode == BeaconMode::Gps && g_config.smart.enabled && !gpsFallback(gpsFix()); }

// Builds the report from GPS (preferred in GPS mode) or the fixed position.
bool buildReport(aprs::PositionReport &r, int &course, String &err) {
    r.symTable = g_config.symbolTable;
    r.symCode = g_config.symbolCode;
    r.comment = g_config.comment.c_str();
    r.messaging = true;
    course = 0;
    const GpsFix f = gpsFix();
    const bool fixedOk = fixedSet();
    if (f.valid && g_config.beaconMode != BeaconMode::Fixed) {
        r.lat = f.lat;
        r.lon = f.lon;
        // Course is meaningless when standing still; leave CSE/SPD out.
        r.hasCourseSpeed = f.speedKmh >= 3;
        r.course = f.course;
        r.speedKnots = f.speedKmh / 1.852;
        course = f.course;
        if (f.hasAlt) {
            r.hasAlt = true;
            r.altFeet = f.altM * 3.28084;
        }
        return true;
    }
    if (fixedOk && (g_config.beaconMode != BeaconMode::Gps || gpsFallback(f))) {
        r.lat = g_config.fixedLat;
        r.lon = g_config.fixedLon;
        if (g_config.fixedAltM) {
            r.hasAlt = true;
            r.altFeet = g_config.fixedAltM * 3.28084;
        }
        return true;
    }
    err = g_config.beaconMode == BeaconMode::Fixed ? "No fixed position set" : "No GPS fix";
    // Beacon now with mode off: use whatever we have.
    if (g_config.beaconMode == BeaconMode::Off && fixedOk) {
        r.lat = g_config.fixedLat;
        r.lon = g_config.fixedLon;
        return true;
    }
    return false;
}

bool send(String &err) {
    aprs::PositionReport r;
    int course;
    if (!buildReport(r, course, err)) return false;
    if (!routerSendInfo(aprs::encodePosition(r), err)) return false;
    s_sb.sent(uptimeS(), course);
    alertPlay(Alert::Beacon);
    return true;
}

}  // namespace

bool beaconNow(String &err) {
    if (const uint32_t wait = holdoffS()) {
        err = "Wait " + String(wait) + " s: one beacon per " + String(kMinBeaconGapS) + " s at most";
        return false;
    }
    const bool ok = send(err);
    s_lastError = ok ? "" : err;
    return ok;
}

void beaconLoop() {
    if (millis() - s_lastCheckMs < 1000) return;
    s_lastCheckMs = millis();
    s_waiting = "";
    const BeaconMode mode = g_config.beaconMode;
    if (mode == BeaconMode::Off) return;
    if (g_config.callsign.isEmpty() || g_config.callsign.equalsIgnoreCase("N0CALL")) {
        s_waiting = "callsign not set";
        return;
    }
    if (!routerCanSend()) {
        const char *rf = g_config.rfLink == RfLink::Lora ? "LoRa radio" : "TNC";
        s_waiting = g_config.aprsIsEnabled ? String(rf) + " or APRS-IS" : String(rf);
        return;
    }
    const uint32_t now = uptimeS();
    bool due;
    const GpsFix f = gpsFix();
    if (mode == BeaconMode::Gps && !gpsFallback(f)) {
        if (!f.valid) {
            s_waiting = f.enabled ? "GPS fix" : "GPS (turned off)";
            return;
        }
        due = useSmart() ? s_sb.due(now, f.speedKmh, f.course, params())
                         : (!s_sb.hasSent() || now - s_sb.lastS() >= g_config.beaconIntervalS);
    } else {
        if (!fixedSet()) {
            s_waiting = "fixed position";
            return;
        }
        if (g_config.beaconIntervalS == 0) return;
        due = !s_sb.hasSent() || now - s_sb.lastS() >= g_config.beaconIntervalS;
    }
    if (!due || holdoffS()) return;
    String err;
    if (!send(err)) {
        s_lastError = err;
        // Don't hammer: count it as a beacon slot.
        s_sb.sent(now, 0);
    } else {
        s_lastError = "";
    }
}

BeaconStatus beaconStatus() {
    BeaconStatus b;
    const BeaconMode m = g_config.beaconMode;
    b.mode = m == BeaconMode::Off ? "off" : m == BeaconMode::Fixed      ? "fixed"
             : gpsFallback(gpsFix()) ? "gps (using fixed)"
             : useSmart()            ? "smart"
                                     : "gps";
    b.sentAny = s_sb.hasSent();
    b.lastAgoS = b.sentAny ? uptimeS() - s_sb.lastS() : 0;
    b.nextInS = -1;
    b.waitingFor = s_waiting;
    b.lastError = s_lastError;
    if (m != BeaconMode::Off && s_waiting.isEmpty()) {
        uint32_t rate = g_config.beaconIntervalS;
        if (useSmart()) rate = aprs::SmartBeacon::rateFor(gpsFix().speedKmh, params());
        if (!b.sentAny) b.nextInS = 0;
        else if (rate) b.nextInS = b.lastAgoS >= rate ? 0 : (int32_t)(rate - b.lastAgoS);
        if (b.nextInS >= 0 && b.nextInS < (int32_t)holdoffS()) b.nextInS = (int32_t)holdoffS();
    }
    return b;
}

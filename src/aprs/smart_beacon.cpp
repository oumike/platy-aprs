#include "smart_beacon.h"
#include <stdlib.h>

namespace aprs {

uint32_t SmartBeacon::rateFor(double speedKmh, const SmartBeaconParams &p) {
    if (speedKmh <= p.slowSpeedKmh) return p.slowRateS;
    if (speedKmh >= p.fastSpeedKmh) return p.fastRateS;
    const uint32_t r = (uint32_t)(p.fastRateS * p.fastSpeedKmh / speedKmh);
    return r > p.slowRateS ? p.slowRateS : r;
}

bool SmartBeacon::due(uint32_t nowS, double speedKmh, int course, const SmartBeaconParams &p) const {
    if (!_sent) return true;
    const uint32_t elapsed = nowS - _lastS;
    if (elapsed >= rateFor(speedKmh, p)) return true;
    // Corner pegging: turning while moving sends early.
    if (speedKmh > p.slowSpeedKmh && elapsed >= p.minTurnTimeS) {
        int diff = abs(course - _lastCourse) % 360;
        if (diff > 180) diff = 360 - diff;
        const double speedMph = speedKmh / 1.609344;
        if (diff > p.minTurnDeg + p.turnSlope / speedMph) return true;
    }
    return false;
}

void SmartBeacon::sent(uint32_t nowS, int course) {
    _sent = true;
    _lastS = nowS;
    _lastCourse = course;
}

}  // namespace aprs

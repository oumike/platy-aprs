#pragma once
#include <stdint.h>

// SmartBeaconing (HamHUD algorithm). Pure C++, host-testable.
namespace aprs {

struct SmartBeaconParams {
    uint32_t slowRateS = 1800, fastRateS = 90;
    double slowSpeedKmh = 5, fastSpeedKmh = 90;
    double minTurnDeg = 28;
    double turnSlope = 255;   // degrees * mph
    uint32_t minTurnTimeS = 15;
};

class SmartBeacon {
public:
    // Seconds between beacons at this speed when going straight.
    static uint32_t rateFor(double speedKmh, const SmartBeaconParams &p);
    // True when a beacon is due now. `course` is only used while moving.
    bool due(uint32_t nowS, double speedKmh, int course, const SmartBeaconParams &p) const;
    void sent(uint32_t nowS, int course);
    bool hasSent() const { return _sent; }
    uint32_t lastS() const { return _lastS; }

private:
    bool _sent = false;
    uint32_t _lastS = 0;
    int _lastCourse = 0;
};

}  // namespace aprs

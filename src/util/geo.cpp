#include "geo.h"
#include <math.h>
#include <stdio.h>

namespace geo {

static constexpr double kPi = 3.14159265358979323846;
static double rad(double d) { return d * kPi / 180.0; }

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double dLat = rad(lat2 - lat1), dLon = rad(lon2 - lon1);
    const double a = sin(dLat / 2) * sin(dLat / 2) +
                     cos(rad(lat1)) * cos(rad(lat2)) * sin(dLon / 2) * sin(dLon / 2);
    return 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    const double y = sin(rad(lon2 - lon1)) * cos(rad(lat2));
    const double x = cos(rad(lat1)) * sin(rad(lat2)) - sin(rad(lat1)) * cos(rad(lat2)) * cos(rad(lon2 - lon1));
    double b = atan2(y, x) * 180.0 / kPi;
    return fmod(b + 360.0, 360.0);
}

const char *compassPoint(double b) {
    static const char *pts[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return pts[(int)floor(fmod(b + 11.25, 360.0) / 22.5) % 16];
}

std::string formatDistance(double km, bool metric) {
    char buf[24];
    if (metric) {
        if (km < 1.0) snprintf(buf, sizeof(buf), "%d m", (int)(km * 1000 + 0.5));
        else snprintf(buf, sizeof(buf), km < 100 ? "%.1f km" : "%.0f km", km);
    } else {
        const double mi = km * 0.621371;
        snprintf(buf, sizeof(buf), mi < 100 ? "%.1f mi" : "%.0f mi", mi);
    }
    return buf;
}

std::string formatSpeed(double knots, bool metric) {
    char buf[20];
    if (metric) snprintf(buf, sizeof(buf), "%.0f km/h", knots * 1.852);
    else snprintf(buf, sizeof(buf), "%.0f mph", knots * 1.15078);
    return buf;
}

std::string formatAltitude(double feet, bool metric) {
    char buf[20];
    if (metric) snprintf(buf, sizeof(buf), "%.0f m", feet * 0.3048);
    else snprintf(buf, sizeof(buf), "%.0f ft", feet);
    return buf;
}

std::string formatAge(unsigned long s) {
    char buf[16];
    if (s < 60) snprintf(buf, sizeof(buf), "%lus", s);
    else if (s < 3600) snprintf(buf, sizeof(buf), "%lum", s / 60);
    else if (s < 86400) snprintf(buf, sizeof(buf), "%luh", s / 3600);
    else snprintf(buf, sizeof(buf), "%lud", s / 86400);
    return buf;
}

}  // namespace geo

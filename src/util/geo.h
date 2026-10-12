#pragma once
#include <string>

namespace geo {

double distanceKm(double lat1, double lon1, double lat2, double lon2);
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
const char *compassPoint(double bearingDeg);   // "N", "NNE", …

// "1.2 mi" / "850 m" etc., by unit system.
std::string formatDistance(double km, bool metric);
std::string formatSpeed(double knots, bool metric);
std::string formatAltitude(double feet, bool metric);
std::string formatAge(unsigned long seconds);   // "12s", "5m", "3h", "2d"

}  // namespace geo

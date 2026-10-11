// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cmath>
namespace acrylic {
inline bool ValidCoordinates(double latitude, double longitude) noexcept {
    return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}
// NOAA fractional-year solar position, with the 90.833-degree sunrise threshold.
// UTC avoids DST/date-line special cases; polar day and night follow elevation.
inline bool SunIsUp(const SYSTEMTIME& utc, double latitude, double longitude) noexcept {
    if (!ValidCoordinates(latitude, longitude)) return false;
    constexpr double pi = 3.14159265358979323846, radians = pi / 180;
    const bool leap = utc.wYear % 4 == 0 && (utc.wYear % 100 != 0 || utc.wYear % 400 == 0);
    constexpr int months[]{0,31,59,90,120,151,181,212,243,273,304,334};
    if (utc.wMonth < 1 || utc.wMonth > 12 || utc.wDay < 1 || utc.wDay > 31) return false;
    const int day = months[utc.wMonth - 1] + utc.wDay + (leap && utc.wMonth > 2 ? 1 : 0);
    const double hour = utc.wHour + utc.wMinute / 60.0 + utc.wSecond / 3600.0;
    const double gamma = 2*pi/(leap ? 366 : 365)*(day-1+(hour-12)/24);
    const double equation = 229.18*(0.000075+0.001868*std::cos(gamma)-0.032077*std::sin(gamma)-0.014615*std::cos(2*gamma)-0.040849*std::sin(2*gamma));
    const double declination = 0.006918-0.399912*std::cos(gamma)+0.070257*std::sin(gamma)-0.006758*std::cos(2*gamma)+0.000907*std::sin(2*gamma)-0.002697*std::cos(3*gamma)+0.00148*std::sin(3*gamma);
    const double angle = (hour*60+equation+4*longitude)/4-180;
    const double elevation = std::sin(latitude*radians)*std::sin(declination)+std::cos(latitude*radians)*std::cos(declination)*std::cos(angle*radians);
    return elevation >= std::cos(90.833*radians);
}
}

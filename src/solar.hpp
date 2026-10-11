// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <vector>
namespace acrylic {
inline bool ValidCoordinates(double latitude, double longitude) noexcept {
    return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}
// NOAA fractional-year solar position, with the 90.833-degree sunrise threshold.
// UTC avoids DST/date-line special cases; polar day and night follow elevation.
inline double SolarElevation(const SYSTEMTIME& utc, double latitude, double longitude) noexcept {
    if (!ValidCoordinates(latitude, longitude)) return -1;
    constexpr double pi = 3.14159265358979323846, radians = pi / 180;
    const bool leap = utc.wYear % 4 == 0 && (utc.wYear % 100 != 0 || utc.wYear % 400 == 0);
    constexpr int months[]{0,31,59,90,120,151,181,212,243,273,304,334};
    if (utc.wMonth < 1 || utc.wMonth > 12 || utc.wDay < 1 || utc.wDay > 31) return -1;
    const int day = months[utc.wMonth - 1] + utc.wDay + (leap && utc.wMonth > 2 ? 1 : 0);
    const double hour = utc.wHour + utc.wMinute / 60.0 + utc.wSecond / 3600.0;
    const double gamma = 2*pi/(leap ? 366 : 365)*(day-1+(hour-12)/24);
    const double equation = 229.18*(0.000075+0.001868*std::cos(gamma)-0.032077*std::sin(gamma)-0.014615*std::cos(2*gamma)-0.040849*std::sin(2*gamma));
    const double declination = 0.006918-0.399912*std::cos(gamma)+0.070257*std::sin(gamma)-0.006758*std::cos(2*gamma)+0.000907*std::sin(2*gamma)-0.002697*std::cos(3*gamma)+0.00148*std::sin(3*gamma);
    const double angle = (hour*60+equation+4*longitude)/4-180;
    const double elevation = std::sin(latitude*radians)*std::sin(declination)+std::cos(latitude*radians)*std::cos(declination)*std::cos(angle*radians);
    return elevation - std::cos(90.833*radians);
}
inline bool SunIsUp(const SYSTEMTIME& utc, double latitude, double longitude) noexcept {
    return SolarElevation(utc, latitude, longitude) >= 0;
}
inline ULONGLONG SolarSeconds(const SYSTEMTIME& utc) noexcept {
    FILETIME file{};
    if (!SystemTimeToFileTime(&utc, &file)) return 0;
    return (static_cast<ULONGLONG>(file.dwHighDateTime) << 32 | file.dwLowDateTime) / 10000000;
}
inline SYSTEMTIME SolarTime(ULONGLONG seconds) noexcept {
    const auto ticks = seconds * 10000000;
    const FILETIME file{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utc{}; FileTimeToSystemTime(&file, &utc); return utc;
}
// Calculate a rolling day of transitions once, then use only cached timestamps.
// Refine extrema as well as sign changes: a very short polar day/night can fall
// entirely between samples. The solar model is approximate; timers resolve to 1s.
class SolarSchedule {
    struct Sample { ULONGLONG time; double elevation; };
    ULONGLONG start_ = 0, end_ = 0;
    double latitude_ = 0, longitude_ = 0;
    bool initialLight_ = false;
    std::vector<ULONGLONG> changes_;
public:
    void Invalidate() noexcept { end_ = 0; }
    ULONGLONG Until() const noexcept { return end_; }
    bool Update(ULONGLONG now, double latitude, double longitude) {
        if (end_ && now >= start_ && now < end_ && latitude == latitude_ && longitude == longitude_) return false;
        start_ = now; end_ = now + 86400; latitude_ = latitude; longitude_ = longitude;
        const auto value = [&](ULONGLONG time) { return SolarElevation(SolarTime(time), latitude, longitude); };
        initialLight_ = value(now) >= 0; changes_.clear();
        std::vector<Sample> samples;
        constexpr ULONGLONG step = 300;
        for (auto time = now - step; time <= end_ + step; time += step) samples.push_back({time, value(time)});
        const auto count = samples.size();
        for (size_t i = 1; i + 1 < count; ++i) {
            const bool peak = samples[i].elevation > samples[i-1].elevation && samples[i].elevation > samples[i+1].elevation;
            const bool valley = samples[i].elevation < samples[i-1].elevation && samples[i].elevation < samples[i+1].elevation;
            if (!peak && !valley) continue;
            auto left = samples[i-1].time, right = samples[i+1].time;
            while (right - left > 3) {
                const auto a = left + (right-left)/3, b = right - (right-left)/3;
                if ((value(a) < value(b)) == peak) left = a; else right = b;
            }
            for (auto time = left; time <= right; ++time) samples.push_back({time, value(time)});
        }
        std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        for (size_t i = 1; i < samples.size(); ++i) {
            const bool light = samples[i-1].elevation >= 0;
            if (light == (samples[i].elevation >= 0)) continue;
            auto left = samples[i-1].time, right = samples[i].time;
            while (right-left > 1) { const auto mid = left+(right-left)/2; if ((value(mid)>=0)==light) left=mid; else right=mid; }
            if (right > now && right < end_ && (changes_.empty() || changes_.back() != right)) changes_.push_back(right);
        }
        return true;
    }
    bool Light(ULONGLONG now) const noexcept {
        return initialLight_ != ((std::upper_bound(changes_.begin(), changes_.end(), now) - changes_.begin()) % 2 != 0);
    }
    ULONGLONG Next(ULONGLONG now) const noexcept {
        const auto next = std::upper_bound(changes_.begin(), changes_.end(), now);
        return next == changes_.end() ? end_ : *next;
    }
};
}

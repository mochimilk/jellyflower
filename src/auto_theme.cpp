// SPDX-License-Identifier: GPL-3.0-or-later
#include "auto_theme.hpp"
#include "solar.hpp"
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <mutex>
#include <chrono>
namespace acrylic {
namespace {
constexpr wchar_t Key[] = L"Software\\jellyflower";
struct Coordinates { double latitude = 0, longitude = 0; };
DWORD Save(bool enabled, const Coordinates* coordinates = nullptr) {
    HKEY key = nullptr;
    auto error = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error) return error;
    if (coordinates) error = RegSetValueExW(key, L"ThemeLocation", 0, REG_BINARY, reinterpret_cast<const BYTE*>(coordinates), sizeof(*coordinates));
    const DWORD value = enabled ? 1 : 0;
    if (!error) error = RegSetValueExW(key, L"AutoTheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    if (!error && !enabled) RegDeleteValueW(key, L"ThemeLocation");
    RegCloseKey(key); return error;
}
}
struct AutoTheme::State {
    std::mutex lock;
    Coordinates coordinates;
    AutoThemeStatus status = AutoThemeStatus::Manual;
    DWORD error = 0;
    UINT generation = 0;
    bool alive = true, reading = false;
    ULONGLONG nextPosition = 0;
    winrt::Windows::Devices::Geolocation::Geolocator locator{nullptr};
    static winrt::fire_and_forget Locate(std::shared_ptr<State> state, UINT generation, bool prompt) {
        using namespace winrt::Windows::Devices::Geolocation;
        AutoThemeStatus status = AutoThemeStatus::Unavailable;
        DWORD error = ERROR_NOT_READY;
        Coordinates coordinates;
        try {
            if (!prompt || co_await Geolocator::RequestAccessAsync() == GeolocationAccessStatus::Allowed) {
                Geolocator locator;
                // City-level accuracy is enough for the solar calculation.
                locator.DesiredAccuracyInMeters(10000);
                {
                    std::lock_guard<std::mutex> lock(state->lock);
                    if (!state->alive || state->generation != generation) co_return;
                    state->locator = locator;
                }
                const auto position = co_await locator.GetGeopositionAsync(std::chrono::hours(1), std::chrono::seconds(15));
                const auto point = position.Coordinate().Point().Position();
                coordinates = {std::round(point.Latitude*10)/10, std::round(point.Longitude*10)/10};
                if (ValidCoordinates(coordinates.latitude, coordinates.longitude)) { status = AutoThemeStatus::Automatic; error = 0; }
            } else { status = AutoThemeStatus::Denied; error = ERROR_ACCESS_DENIED; }
        } catch (const winrt::hresult_error& failure) {
            error = static_cast<DWORD>(failure.code());
            status = failure.code() == E_ACCESSDENIED ? AutoThemeStatus::Denied : AutoThemeStatus::Unavailable;
        } catch (...) { error = ERROR_GEN_FAILURE; }
        std::lock_guard<std::mutex> lock(state->lock);
        if (!state->alive || state->generation != generation) co_return;
        state->reading = false;
        if (!error) {
            error = Save(true, &coordinates);
            if (!error) state->coordinates = coordinates;
            else status = AutoThemeStatus::Error;
        }
        if (error && (status == AutoThemeStatus::Denied || (prompt && status != AutoThemeStatus::Automatic))) Save(false);
        // With a transient GPS/network failure, retain an already approved
        // location. Revoked permission always stops automatic changes.
        if (!prompt && error && status != AutoThemeStatus::Denied && state->status == AutoThemeStatus::Automatic) status = AutoThemeStatus::Automatic;
        state->status = status; state->error = error;
        state->nextPosition = GetTickCount64() + (error ? 15*60*1000 : 24*60*60*1000);
    }
};
AutoTheme::AutoTheme() : state_(std::make_shared<State>()) {
    DWORD enabled = 0, size = sizeof(enabled);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"AutoTheme", RRF_RT_REG_DWORD, nullptr, &enabled, &size) || enabled != 1) return;
    size = sizeof(Coordinates);
    if (!RegGetValueW(HKEY_CURRENT_USER, Key, L"ThemeLocation", RRF_RT_REG_BINARY, nullptr, &state_->coordinates, &size)
        && size == sizeof(Coordinates) && ValidCoordinates(state_->coordinates.latitude, state_->coordinates.longitude)) {
        // A background launch never requests new consent. Revalidate the
        // previously approved location before resuming the schedule.
        state_->status = AutoThemeStatus::Locating;
        state_->reading = true;
        State::Locate(state_, state_->generation, false);
    }
}
AutoTheme::~AutoTheme() { std::lock_guard<std::mutex> lock(state_->lock); state_->alive = false; ++state_->generation; }
void AutoTheme::Enable() {
    UINT generation = 0;
    {
        std::lock_guard<std::mutex> lock(state_->lock);
        if (state_->reading) return;
        generation = ++state_->generation; state_->reading = true;
        state_->status = AutoThemeStatus::Locating; state_->error = 0;
    }
    State::Locate(state_, generation, true);
}
DWORD AutoTheme::Disable() {
    std::lock_guard<std::mutex> lock(state_->lock);
    const DWORD error = Save(false);
    if (!error) {
        ++state_->generation; state_->reading = false;
        state_->status = AutoThemeStatus::Manual; state_->error = 0; state_->locator = nullptr;
    }
    return error;
}
bool AutoTheme::Tick(bool& light) {
    UINT generation = 0;
    bool refresh = false, automatic = false;
    {
        std::lock_guard<std::mutex> lock(state_->lock);
        if (state_->status == AutoThemeStatus::Automatic) {
            try {
                if (state_->locator && state_->locator.LocationStatus() == winrt::Windows::Devices::Geolocation::PositionStatus::Disabled) {
                    state_->status = AutoThemeStatus::Denied; state_->error = ERROR_ACCESS_DENIED; Save(false); return false;
                }
            } catch (const winrt::hresult_error& failure) {
                state_->error = static_cast<DWORD>(failure.code());
                if (failure.code() == E_ACCESSDENIED) { state_->status = AutoThemeStatus::Denied; Save(false); return false; }
            }
            SYSTEMTIME now{}; GetSystemTime(&now);
            light = SunIsUp(now, state_->coordinates.latitude, state_->coordinates.longitude); automatic = true;
            if (!state_->reading && GetTickCount64() >= state_->nextPosition) {
                generation = state_->generation; state_->reading = true; refresh = true;
            }
        }
    }
    if (refresh) State::Locate(state_, generation, false);
    return automatic;
}
AutoThemeStatus AutoTheme::Status() const { std::lock_guard<std::mutex> lock(state_->lock); return state_->status; }
DWORD AutoTheme::Error() const { std::lock_guard<std::mutex> lock(state_->lock); return state_->error; }
}

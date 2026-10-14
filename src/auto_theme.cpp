// SPDX-License-Identifier: GPL-3.0-or-later
#include "auto_theme.hpp"
#include "solar.hpp"
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <mutex>
#include <chrono>
#include <utility>
namespace acrylic {
namespace {
constexpr wchar_t Key[] = L"Software\\jellyflower";
struct Coordinates { double latitude = 0, longitude = 0; };
DWORD Save(bool enabled, const Coordinates* coordinates = nullptr,AutoThemeScope scope=AutoThemeScope::System) {
    HKEY key = nullptr;
    auto error = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error) return error;
    if (coordinates) error = RegSetValueExW(key, AutoLocationValue(scope), 0, REG_BINARY, reinterpret_cast<const BYTE*>(coordinates), sizeof(*coordinates));
    const DWORD value = enabled ? 1 : 0;
    if (!error) error = RegSetValueExW(key, AutoThemeValue(scope), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    if (!error && !enabled) RegDeleteValueW(key, AutoLocationValue(scope));
    RegCloseKey(key); return error;
}
void CancelLocation(const winrt::Windows::Foundation::IAsyncInfo& operation)noexcept{
    if(operation)try{operation.Cancel();}catch(...){/* A completed/disconnected request needs no further work. */}
}
}
struct AutoTheme::State {
    AutoThemeScope scope=AutoThemeScope::System;
    std::mutex lock;
    Coordinates coordinates;
    AutoThemeStatus status = AutoThemeStatus::Manual;
    DWORD error = 0;
    UINT generation = 0;
    bool alive = true, reading = false;
    ULONGLONG nextPosition = 0;
    HWND window = nullptr;
    SolarSchedule schedule;
    // Only an in-flight one-shot operation is retained, for cancellation.
    // There is no locator or location/status subscription between readings.
    winrt::Windows::Foundation::IAsyncInfo pending{nullptr};
    bool Track(const winrt::Windows::Foundation::IAsyncInfo& operation,UINT request){
        std::lock_guard<std::mutex> guard(lock);
        if(!alive || generation!=request)return false;
        pending=operation;return true;
    }
    void Notify() const noexcept { if (alive && window) PostMessageW(window, scope==AutoThemeScope::System?AutoThemeChangedMessage:WallpaperAutoThemeChangedMessage, 0, 0); }
    static winrt::fire_and_forget Locate(std::shared_ptr<State> state, UINT generation, bool prompt) {
        using namespace winrt::Windows::Devices::Geolocation;
        AutoThemeStatus status = AutoThemeStatus::Unavailable;
        DWORD error = ERROR_NOT_READY;
        Coordinates coordinates;
        try {
            bool allowed=!prompt;
            if(prompt){
                const auto access=Geolocator::RequestAccessAsync();
                if(!state->Track(access.as<winrt::Windows::Foundation::IAsyncInfo>(),generation)){CancelLocation(access.as<winrt::Windows::Foundation::IAsyncInfo>());co_return;}
                allowed=co_await access==GeolocationAccessStatus::Allowed;
            }
            if(allowed){
                {std::lock_guard<std::mutex> lock(state->lock);if(!state->alive || state->generation!=generation)co_return;}
                Geolocator locator;
                // City-level accuracy is enough for the solar calculation.
                locator.DesiredAccuracyInMeters(10000);
                const auto request=locator.GetGeopositionAsync(std::chrono::hours(1),std::chrono::seconds(15));
                if(!state->Track(request.as<winrt::Windows::Foundation::IAsyncInfo>(),generation)){CancelLocation(request.as<winrt::Windows::Foundation::IAsyncInfo>());co_return;}
                const auto position = co_await request;
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
        state->pending=nullptr;
        state->reading = false;
        if (!error) {
            error = Save(true, &coordinates,state->scope);
            if (!error) {
                if (state->coordinates.latitude != coordinates.latitude || state->coordinates.longitude != coordinates.longitude)
                    state->schedule.Invalidate();
                state->coordinates = coordinates;
            }
            else status = AutoThemeStatus::Error;
        }
        if (error && (status == AutoThemeStatus::Denied || (prompt && status != AutoThemeStatus::Automatic))) Save(false,nullptr,state->scope);
        // Transient one-shot failures retain the approved cached coordinates.
        // Permission denial at a reading stops automatic changes.
        if (!prompt && error && status != AutoThemeStatus::Denied && state->status == AutoThemeStatus::Automatic) status = AutoThemeStatus::Automatic;
        state->status = status; state->error = error;
        state->nextPosition = GetTickCount64() + (error ? 15*60*1000 : 24*60*60*1000);
        state->Notify();
    }
};
AutoTheme::AutoTheme(HWND window,AutoThemeScope scope) : state_(std::make_shared<State>()) {
    state_->window = window;state_->scope=scope;
    DWORD enabled = 0, size = sizeof(enabled);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, AutoThemeValue(scope), RRF_RT_REG_DWORD, nullptr, &enabled, &size) || enabled != 1) return;
    size = sizeof(Coordinates);
    if (!RegGetValueW(HKEY_CURRENT_USER, Key, AutoLocationValue(scope), RRF_RT_REG_BINARY, nullptr, &state_->coordinates, &size)
        && size == sizeof(Coordinates) && ValidCoordinates(state_->coordinates.latitude, state_->coordinates.longitude)) {
        // A background launch never requests new consent. Revalidate the
        // previously approved location before resuming the schedule.
        state_->status = AutoThemeStatus::Locating;
        state_->reading = true;
        State::Locate(state_, state_->generation, false);
    }
}
AutoTheme::~AutoTheme() {
    winrt::Windows::Foundation::IAsyncInfo pending{nullptr};
    {std::lock_guard<std::mutex> lock(state_->lock);
        state_->alive=false;state_->window=nullptr;++state_->generation;pending=std::exchange(state_->pending,nullptr);}
    CancelLocation(pending);
}
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
    winrt::Windows::Foundation::IAsyncInfo pending{nullptr};DWORD error=0;
    {std::lock_guard<std::mutex> lock(state_->lock);
        error=Save(false,nullptr,state_->scope);
        if(!error){++state_->generation;state_->reading=false;state_->status=AutoThemeStatus::Manual;state_->error=0;
            pending=std::exchange(state_->pending,nullptr);state_->schedule.Invalidate();}}
    CancelLocation(pending);
    return error;
}
bool AutoTheme::Update(bool& light, UINT& delay, bool invalidate) {
    UINT generation = 0;
    bool refresh = false, automatic = false;
    delay = 0;
    {
        std::lock_guard<std::mutex> lock(state_->lock);
        if (state_->status == AutoThemeStatus::Automatic) {
            SYSTEMTIME utc{}; GetSystemTime(&utc);
            const auto now = SolarSeconds(utc);
            if (invalidate) state_->schedule.Invalidate();
            state_->schedule.Update(now, state_->coordinates.latitude, state_->coordinates.longitude);
            light = state_->schedule.Light(now); automatic = true;
            const auto tick = GetTickCount64();
            if (!state_->reading && tick >= state_->nextPosition) {
                generation = state_->generation; state_->reading = true; refresh = true;
            }
            auto wait = (state_->schedule.Next(now) - now)*1000 - utc.wMilliseconds;
            if (!state_->reading) wait = std::min(wait, state_->nextPosition-tick);
            delay = static_cast<UINT>(std::max<ULONGLONG>(1, wait));
        }
    }
    if (refresh) State::Locate(state_, generation, false);
    return automatic;
}
AutoThemeStatus AutoTheme::Status() const { std::lock_guard<std::mutex> lock(state_->lock); return state_->status; }
DWORD AutoTheme::Error() const { std::lock_guard<std::mutex> lock(state_->lock); return state_->error; }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace acrylic {
inline constexpr UINT WallpaperChangedMessage = WM_APP + 68;
enum class WallpaperStatus : UINT { Off, On, Applying, Restoring, ErrorOff, ErrorOn };
inline bool WallpaperBusy(WallpaperStatus state) {
    return state == WallpaperStatus::Applying || state == WallpaperStatus::Restoring;
}
inline const wchar_t* WallpaperLabel(WallpaperStatus state) {
    return state == WallpaperStatus::ErrorOff || state == WallpaperStatus::ErrorOn ? L"try again"
        : state == WallpaperStatus::On || state == WallpaperStatus::Applying ? L"on" : L"off";
}
struct WallpaperMonitor { std::wstring id, image, backup; };
struct WallpaperDesktop {
    std::vector<WallpaperMonitor> monitors;
    DWORD position = 0;
};
struct WallpaperState { bool enabled = false; WallpaperDesktop previous; };

// Separate the persistent ownership policy from Windows/file side effects.
struct WallpaperBackend {
    virtual ~WallpaperBackend() = default;
    virtual HRESULT Load(WallpaperState&) = 0;
    virtual HRESULT Save(const WallpaperState&) = 0;
    virtual HRESULT Capture(WallpaperDesktop&, bool backup) = 0;
    virtual HRESULT Assets(std::wstring& light, std::wstring& dark) = 0;
    virtual HRESULT Apply(const std::wstring& image) = 0;
    virtual HRESULT Restore(const WallpaperDesktop&, const std::vector<std::wstring>& monitors, bool position) = 0;
};
class WallpaperEngine {
    WallpaperBackend& backend;
    WallpaperState state;
public:
    explicit WallpaperEngine(WallpaperBackend& value) : backend(value) {}
    HRESULT Load() { return backend.Load(state); }
    bool Enabled() const { return state.enabled; }
    HRESULT Update(bool enabled, bool light);
};

// One sleeping worker, woken only by actions, theme changes and display events.
// No wallpaper work runs on the menu thread and no polling timer is added.
class WallpaperManager {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit WallpaperManager(HWND owner, bool light, std::unique_ptr<WallpaperBackend> backend = {});
    ~WallpaperManager();
    WallpaperStatus Status() const;
    HRESULT Error() const;
    void Toggle(bool light);
    void Refresh(bool light);
};
} // namespace acrylic

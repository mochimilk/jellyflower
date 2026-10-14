// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include <cwchar>

namespace acrylic {
inline constexpr UINT WallpaperChangedMessage = WM_APP + 68;
enum class WallpaperStatus : UINT { Off, On, Applying, Restoring, ErrorOff, ErrorOn };
inline bool WallpaperBusy(WallpaperStatus state) {
    return state == WallpaperStatus::Applying || state == WallpaperStatus::Restoring;
}
inline constexpr const wchar_t* WallpaperErrorLabels[]{L"try again",L"image missing",L"not allowed",L"file in use",L"storage full",L"image too large",L"backup issue",L"error 88888888",L"error aaaaaaaa",L"error bbbbbbbb",L"error cccccccc",L"error dddddddd",L"error eeeeeeee",L"error ffffffff"};
inline const wchar_t* WallpaperLabel(WallpaperStatus state,HRESULT error=S_OK) {
    if(state==WallpaperStatus::ErrorOff || state==WallpaperStatus::ErrorOn){
        if(error==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || error==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))return WallpaperErrorLabels[1];
        if(error==E_ACCESSDENIED || error==HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY))return WallpaperErrorLabels[2];
        if(error==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) || error==HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION))return WallpaperErrorLabels[3];
        if(error==HRESULT_FROM_WIN32(ERROR_DISK_FULL) || error==HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL))return WallpaperErrorLabels[4];
        if(error==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))return WallpaperErrorLabels[5];
        if(error==E_INVALIDARG)return WallpaperErrorLabels[6];
        if(FAILED(error)){thread_local wchar_t detail[24]{};swprintf_s(detail,L"error %08lx",static_cast<unsigned long>(error));return detail;}
        return WallpaperErrorLabels[0];
    }
    return state==WallpaperStatus::On || state==WallpaperStatus::Applying?L"on":L"off";
}
// Attachment is a live observation, not part of the persisted v1 backup format.
struct WallpaperMonitor { std::wstring id, image, backup; bool attached=true; };
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
    virtual HRESULT Apply(const std::wstring& image,const std::vector<std::wstring>& monitors) = 0;
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

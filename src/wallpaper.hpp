// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "localization.hpp"
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
inline std::vector<std::wstring> WallpaperStatusLabels() {
    std::vector<std::wstring> labels;
    for(const auto id:{TextId::TryAgain,TextId::ImageMissing,TextId::NotAllowed,TextId::FileInUse,TextId::StorageFull,TextId::ImageTooLarge,TextId::BackupIssue})labels.emplace_back(Tr(id));
    for(const auto code:{L"88888888",L"aaaaaaaa",L"bbbbbbbb",L"cccccccc",L"dddddddd",L"eeeeeeee",L"ffffffff"})labels.push_back(Text(TextId::ErrorCode,{{L"code",code}}));
    return labels;
}
inline const wchar_t* WallpaperLabel(WallpaperStatus state,HRESULT error=S_OK) {
    if(state==WallpaperStatus::ErrorOff || state==WallpaperStatus::ErrorOn){
        if(error==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || error==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))return Tr(TextId::ImageMissing);
        if(error==E_ACCESSDENIED || error==HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY))return Tr(TextId::NotAllowed);
        if(error==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) || error==HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION))return Tr(TextId::FileInUse);
        if(error==HRESULT_FROM_WIN32(ERROR_DISK_FULL) || error==HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL))return Tr(TextId::StorageFull);
        if(error==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))return Tr(TextId::ImageTooLarge);
        if(error==E_INVALIDARG)return Tr(TextId::BackupIssue);
        if(FAILED(error)){thread_local std::wstring detail;wchar_t code[9]{};swprintf_s(code,L"%08lx",static_cast<unsigned long>(error));detail=Text(TextId::ErrorCode,{{L"code",code}});return detail.c_str();}
        return Tr(TextId::TryAgain);
    }
    return OnOff(state==WallpaperStatus::On || state==WallpaperStatus::Applying);
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
    virtual const wchar_t* Stage() const noexcept { return L"wallpaper"; }
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
    const wchar_t* failureStage=nullptr;
public:
    explicit WallpaperEngine(WallpaperBackend& value) : backend(value) {}
    HRESULT Load() { return backend.Load(state); }
    bool Enabled() const { return state.enabled; }
    const wchar_t* ErrorStage() const noexcept {return failureStage?failureStage:backend.Stage();}
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
    const wchar_t* ErrorStage() const;
    void Toggle(bool light);
    void Refresh(bool light);
};
} // namespace acrylic

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include "appearance.hpp"

namespace acrylic {
inline constexpr wchar_t WindowClass[] = L"AcrylicTaskbar.Controller.v1";
inline constexpr wchar_t RunValue[] = L"jellyflower";
inline constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr GUID TapClass = {0x6b57d915, 0xcb16, 0x4f42,
    {0x97, 0x4d, 0x3a, 0xc8, 0xef, 0x2b, 0x06, 0x52}};
inline constexpr GUID PowerSavingGuid = {0xe00958c0, 0xc213, 0x4ace,
    {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
inline constexpr LONG Magic = 0x41544240;
inline constexpr UINT QueryMessage = WM_APP + 42;
enum Query : WPARAM { BackendQuery, AppliedQuery, ResultQuery, BridgeQuery, EnabledQuery, TrayQuery,
    CursorModeQuery, CursorAppliedQuery, CursorErrorQuery, BorderQuery, LightQuery, AcrylicModeQuery,
    ClockQuery, ClockResultQuery, VolumeQuery, VolumeResultQuery, IslandsQuery, RetainedQuery,
    ClockDateQuery, StartCountQuery, StartAttachedQuery, StartResultQuery, StartSearchQuery, StartSpacingQuery,
    SearchAttachedQuery,SearchInputQuery,SearchResultQuery,WallpaperStatusQuery,WallpaperResultQuery,
    AdaptiveGlyphQuery,AdaptiveBitmapQuery,AdaptiveResultQuery };
enum Backend : LONG { Waiting, Legacy, Modern, Unsupported };
enum BridgeState : LONG { Idle, Starting, Attached, Failed };

// Same bitness on both sides; only aligned 32-bit interlocked fields are shared.
struct SharedState {
    volatile LONG magic;
    volatile LONG controllerPid;
    volatile LONG epoch;
    volatile LONG active; // AcrylicMode: off / system / pure, published atomically.
    volatile LONG bridgeState;
    volatile LONG appliedCount;
    volatile LONG lastResult;
    volatile LONG borderCount;
    volatile LONG running; // Controller lifetime is independent of acrylic off.
    volatile LONG clockCount;
    volatile LONG clockResult;
    volatile LONG volumeCount;
    volatile LONG volumeResult;
    volatile LONG clockOptions; // Date bit; retired font bits are ignored.
    volatile LONG adaptiveGlyphs;
    volatile LONG adaptiveBitmaps;
    volatile LONG adaptiveResult;
};
inline LONG Read(volatile LONG& value) noexcept {
    return InterlockedCompareExchange(&value, 0, 0);
}
inline std::wstring MappingName(DWORD explorerPid) {
    return L"Local\\AcrylicTaskbar.Bridge.v16." + std::to_wstring(explorerPid);
}

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) noexcept {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};
inline bool OlderBridgePresent(DWORD explorerPid) {
    for (UINT version = 1; version < 16; ++version) {
        Handle older(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
            (L"Local\\AcrylicTaskbar.Bridge.v" + std::to_wstring(version) + L"." + std::to_wstring(explorerPid)).c_str()));
        if (!older) continue;
        if(version!=15)return true;
        // The v15 ABI is known. A normally stopped old controller can retire
        // its presentation synchronously before the uniquely named v16 DLL
        // takes over. Unknown/live generations still require a shell reload.
        auto old=static_cast<volatile LONG*>(MapViewOfFile(older.get(),FILE_MAP_ALL_ACCESS,0,0,14*sizeof(LONG)));
        if(!old)return true;
        bool retired=Read(old[0])==0x4154423F && Read(old[8])==0;
        if(retired){HWND receiver=nullptr;while((receiver=FindWindowExW(HWND_MESSAGE,receiver,L"AcrylicTaskbar.BridgeNotify.v15",nullptr))){
            DWORD pid=0;GetWindowThreadProcessId(receiver,&pid);if(pid!=explorerPid)continue;
            DWORD_PTR result=0;if(!SendMessageTimeoutW(receiver,WM_APP+71,0,0,SMTO_ABORTIFHUNG,500,&result)){retired=false;break;}
        }}
        retired=retired && Read(old[5])==0 && Read(old[7])==0 && Read(old[9])==0 && Read(old[11])==0;
        UnmapViewOfFile(const_cast<LONG*>(old));if(!retired)return true;
    }
    return false;
}
} // namespace acrylic

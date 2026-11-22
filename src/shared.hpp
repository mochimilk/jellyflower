// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include "appearance.hpp"

namespace acrylic {
inline constexpr wchar_t WindowClass[] = L"AcrylicTaskbar.Controller.v1";
inline constexpr wchar_t RunValue[] = L"jellyflower";
inline constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr GUID TapClass = {0x3fe9d131, 0x3885, 0x4ebf,
    {0xbf, 0x42, 0x09, 0x54, 0xe7, 0xc9, 0x7e, 0xef}};
inline constexpr GUID PowerSavingGuid = {0xe00958c0, 0xc213, 0x4ace,
    {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
inline constexpr LONG Magic = 0x41544245;
inline constexpr UINT QueryMessage = WM_APP + 42;
enum Query : WPARAM { BackendQuery, AppliedQuery, ResultQuery, BridgeQuery, EnabledQuery, TrayQuery,
    CursorModeQuery, CursorAppliedQuery, CursorErrorQuery, BorderQuery, LightQuery, AcrylicModeQuery,
    ClockQuery, ClockResultQuery, VolumeQuery, VolumeResultQuery, IslandsQuery, RetainedQuery,
    ClockDateQuery, StartCountQuery, StartAttachedQuery, StartResultQuery, StartSearchQuery, StartSpacingQuery,
    SearchAttachedQuery,SearchInputQuery,SearchResultQuery,WallpaperStatusQuery,WallpaperResultQuery,
    AdaptiveGlyphQuery,AdaptiveBitmapQuery,AdaptiveResultQuery,TrayOnlyQuery,TrayHiddenQuery,TrayFlowerQuery,
    TrayResultQuery, KeyboardLayoutQuery, KeyboardAttachedQuery, KeyboardCountQuery, KeyboardResultQuery, KeyboardFieldQuery,
    CompactDisplaysQuery, StatusBarQuery, CompactResultQuery, DockQuery, DockCountQuery, DockRegionQuery, DockResultQuery,
    BarHeightQuery, BarHeightResultQuery, MenuGlassResultQuery, FlutedQuery, FlutedResultQuery, TopBarQuery, TopBarDisplaysQuery };
enum Backend : LONG { Waiting, Legacy, Modern, Unsupported };
enum BridgeState : LONG { Idle, Starting, Attached, Failed };

// Same bitness on both sides; only aligned 32-bit interlocked fields are shared.
struct SharedState {
    volatile LONG magic;
    volatile LONG controllerPid;
    volatile LONG epoch;
    volatile LONG active; // AcrylicMode: off / system / pure / fluted, published atomically.
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
    volatile LONG trayOptions; // Bit 0: only jellyflower's notification icon. Bit 1 (v20): the menu is in the status bar.
    volatile LONG trayHidden;
    volatile LONG trayFlower;
    volatile LONG trayResult;
    volatile LONG dockOptions; // Bit 0: floating dock. Bit 1: Windows auto hide is on.
    volatile LONG dockCount;   // Taskbars drawn as a dock.
    volatile LONG dockRegions; // Taskbars whose empty parts are click-through.
    volatile LONG dockResult;
    // The dock's taller bar (bar_height.hpp): the controller publishes the
    // height in DIPs (0 stock) and where TrayUI::GetMinSize sits in
    // Taskbar.dll; the helper reports the height it answers and its result.
    volatile LONG barHeight;
    volatile LONG minSizeSlot;
    volatile LONG minSizeFunction;
    volatile LONG barHeightShown;
    volatile LONG barHeightResult;
    volatile LONG flutedCount; // Taskbar backgrounds showing fluted glass (v21).
    volatile LONG flutedResult;
    // Where the controller draws each taskbar's fluted glass: glass_state.hpp.
    volatile LONG glassSequence;
    volatile LONG glassSlots[4][6];
    volatile LONG glassShown;
    volatile LONG glassResult;
};
inline LONG Read(volatile LONG& value) noexcept {
    return InterlockedCompareExchange(&value, 0, 0);
}
inline bool DockOn(SharedState& state) noexcept { return (Read(state.dockOptions) & 1) != 0; }
// The notification area has no taskbar fill behind it: acrylic pure, or the
// floating dock, which leaves it over the wallpaper.
inline bool BareTray(SharedState& state) noexcept {
    return DecodeAcrylicMode(Read(state.active)) == AcrylicMode::Pure || DockOn(state);
}
inline std::wstring MappingName(DWORD explorerPid) {
    return L"Local\\AcrylicTaskbar.Bridge.v21." + std::to_wstring(explorerPid);
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
    for (UINT version = 1; version < 21; ++version) {
        Handle older(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
            (L"Local\\AcrylicTaskbar.Bridge.v" + std::to_wstring(version) + L"." + std::to_wstring(explorerPid)).c_str()));
        if (!older) continue;
        if(version<15)return true;
        // The v15 through v20 ABIs are known. A normally stopped old controller
        // can retire its presentation synchronously before the uniquely named
        // v21 DLL takes over. Unknown/live generations still require a shell reload.
        auto old=static_cast<volatile LONG*>(MapViewOfFile(older.get(),FILE_MAP_ALL_ACCESS,0,0,(version==15?14:version<19?17:version<20?21:30)*sizeof(LONG)));
        if(!old)return true;
        const LONG magic=version==15?0x4154423F:version==16?0x41544240:version==17?0x41544241:version==18?0x41544242:version==19?0x41544243:0x41544244;
        bool retired=Read(old[0])==magic && Read(old[8])==0;
        const auto receiverClass=L"AcrylicTaskbar.BridgeNotify.v"+std::to_wstring(version);
        if(retired){HWND receiver=nullptr;while((receiver=FindWindowExW(HWND_MESSAGE,receiver,receiverClass.c_str(),nullptr))){
            DWORD pid=0;GetWindowThreadProcessId(receiver,&pid);if(pid!=explorerPid)continue;
            DWORD_PTR result=0;if(!SendMessageTimeoutW(receiver,WM_APP+71,0,0,SMTO_ABORTIFHUNG,500,&result)){retired=false;break;}
        }}
        retired=retired && Read(old[5])==0 && Read(old[7])==0 && Read(old[9])==0 && Read(old[11])==0;
        if(version>=16)retired=retired && Read(old[14])==0 && Read(old[15])==0;
        if(version>=19)retired=retired && Read(old[18])==0; // trayHidden
        if(version>=20)retired=retired && Read(old[22])==0 && Read(old[23])==0 && Read(old[28])==0; // dockCount, dockRegions, barHeightShown
        UnmapViewOfFile(const_cast<LONG*>(old));if(!retired)return true;
    }
    return false;
}
} // namespace acrylic

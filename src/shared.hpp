// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include "appearance.hpp"

namespace acrylic {
inline constexpr wchar_t WindowClass[] = L"AcrylicTaskbar.Controller.v1";
inline constexpr wchar_t RunValue[] = L"jellyflower";
inline constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr GUID TapClass = {0xf43a8b72, 0xcb16, 0x4f42,
    {0x97, 0x4d, 0x3a, 0xc8, 0xef, 0x2b, 0x06, 0x52}};
inline constexpr GUID PowerSavingGuid = {0xe00958c0, 0xc213, 0x4ace,
    {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
inline constexpr LONG Magic = 0x4154423E;
inline constexpr UINT QueryMessage = WM_APP + 42;
enum Query : WPARAM { BackendQuery, AppliedQuery, ResultQuery, BridgeQuery, EnabledQuery, TrayQuery,
    CursorModeQuery, CursorAppliedQuery, CursorErrorQuery, BorderQuery, LightQuery, AcrylicModeQuery,
    ClockQuery, ClockResultQuery, VolumeQuery, VolumeResultQuery, IslandsQuery, RetainedQuery,
    ClockDateQuery, StartCountQuery, StartAttachedQuery, StartResultQuery, StartSearchQuery, StartSpacingQuery,
    SearchAttachedQuery,SearchInputQuery,SearchResultQuery };
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
};
inline LONG Read(volatile LONG& value) noexcept {
    return InterlockedCompareExchange(&value, 0, 0);
}
inline std::wstring MappingName(DWORD explorerPid) {
    return L"Local\\AcrylicTaskbar.Bridge.v14." + std::to_wstring(explorerPid);
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
    for (UINT version = 1; version < 14; ++version) {
        Handle older(OpenFileMappingW(FILE_MAP_READ, FALSE,
            (L"Local\\AcrylicTaskbar.Bridge.v" + std::to_wstring(version) + L"." + std::to_wstring(explorerPid)).c_str()));
        if (older) return true;
    }
    return false;
}
} // namespace acrylic

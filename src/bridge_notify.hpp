// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"

namespace acrylic {
inline constexpr wchar_t BridgeNotifyClass[] = L"AcrylicTaskbar.BridgeNotify.v10";
inline constexpr UINT BridgeRefreshMessage = WM_APP + 71;
using BridgeRefreshCallback = void (*)() noexcept;

// One invisible receiver per XAML UI thread. The callback runs on that thread.
HWND EnsureBridgeNotifyWindow(HINSTANCE module, BridgeRefreshCallback refresh) noexcept;
// Publish before posting; unchanged state causes no extra idle wakeups.
void SetBridgeMode(SharedState* state, DWORD explorerPid, AcrylicMode mode, bool forceNotify = false) noexcept;
}

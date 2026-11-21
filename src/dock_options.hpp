// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// The dock's bar height in DIPs (the stock bar is 48): room for the pill to
// float with its icons centered and their highlights square.
inline constexpr LONG DockBarHeight = 64;
// Off by default: the stock full-width taskbar stays until the user asks for
// the floating dock.
inline bool LoadDock() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"Dock", RRF_RT_REG_DWORD, nullptr, &value, &size)) return false;
    return value == 1;
}
inline DWORD SaveDock(bool enabled) {
    const DWORD value = enabled ? 1 : 0;
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"Dock", REG_DWORD, &value, sizeof(value)));
}
}

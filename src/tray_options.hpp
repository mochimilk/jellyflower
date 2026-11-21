// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// Off by default: the stock notification area stays until the user asks for
// jellyflower's icon alone.
inline bool LoadTrayOnly() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"TrayOnly", RRF_RT_REG_DWORD, nullptr, &value, &size)) return false;
    return value == 1;
}
inline DWORD SaveTrayOnly(bool enabled) {
    const DWORD value = enabled ? 1 : 0;
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"TrayOnly", REG_DWORD, &value, sizeof(value)));
}
}

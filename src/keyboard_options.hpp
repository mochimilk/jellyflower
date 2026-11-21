// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// Off by default: the touch keyboard keeps Windows' layout until the user
// chooses the symmetric one.
inline bool LoadKeyboardLayout() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"KeyboardLayout", RRF_RT_REG_DWORD, nullptr, &value, &size)) return false;
    return value == 1;
}
inline DWORD SaveKeyboardLayout(bool symmetric) {
    const DWORD value = symmetric ? 1 : 0;
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"KeyboardLayout", REG_DWORD, &value, sizeof(value)));
}
}

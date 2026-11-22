// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// An on/off preference, a DWORD under HKCU\Software\jellyflower. A missing or
// unreadable value is the default, and only the exact value that leaves it
// counts: 1 turns an opt-in on, 0 turns an opt-out off.
struct BoolSetting {
    const wchar_t* name;
    bool fallback;
    bool Load() const {
        DWORD value = 0, size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", name, RRF_RT_REG_DWORD, nullptr, &value, &size)) return fallback;
        return fallback ? value != 0 : value == 1;
    }
    DWORD Save(bool enabled) const {
        const DWORD value = enabled ? 1 : 0;
        return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", name, REG_DWORD, &value, sizeof(value)));
    }
};
} // namespace acrylic

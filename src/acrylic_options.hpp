// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "appearance.hpp"
#include <windows.h>

namespace acrylic {
// The taskbar's material, as the menu's taskbar > acrylic row last set it:
// fluted when nothing is saved.
inline AcrylicMode LoadAcrylicMode() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"Acrylic", RRF_RT_REG_DWORD, nullptr, &value, &size)) return AcrylicMode::Fluted;
    return DecodeAcrylicMode(static_cast<LONG>(value));
}
inline DWORD SaveAcrylicMode(AcrylicMode mode) {
    const DWORD value = static_cast<DWORD>(mode);
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"Acrylic", REG_DWORD, &value, sizeof(value)));
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
namespace acrylic {
inline constexpr wchar_t PersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
inline bool SystemLightTheme() {
    DWORD light = 0, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, PersonalizeKey,
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    return light != 0;
}
struct ThemeChangeResult {
    DWORD error = ERROR_SUCCESS, rollbackError = ERROR_SUCCESS, notificationError = ERROR_SUCCESS;
};
// Changes the account's system and app preference; does not change contrast themes.
ThemeChangeResult SetWindowsLightTheme(bool light);
} // namespace acrylic

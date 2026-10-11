// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.hpp"

namespace acrylic {
namespace {
constexpr wchar_t SystemValue[] = L"SystemUsesLightTheme", AppsValue[] = L"AppsUseLightTheme";
struct Preference { DWORD value = 0; bool exists = false; };
LSTATUS ReadPreference(HKEY key, const wchar_t* name, Preference& preference) {
    DWORD bytes = sizeof(preference.value);
    const LSTATUS result = RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &preference.value, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (result != ERROR_SUCCESS) return result;
    if (bytes != sizeof(DWORD) || preference.value > 1) return ERROR_INVALID_DATA;
    preference.exists = true;
    return ERROR_SUCCESS;
}
LSTATUS WritePreference(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}
DWORD WINAPI NotifyThemeChange(void*) {
    // Notify on a worker so a slow third-party window cannot freeze our menu.
    // The string has static lifetime throughout the synchronous broadcast.
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"), SMTO_ABORTIFHUNG, 1000, nullptr);
    SendMessageTimeoutW(HWND_BROADCAST, WM_THEMECHANGED, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
    return 0;
}
} // namespace

ThemeChangeResult SetWindowsLightTheme(bool light) {
    ThemeChangeResult result;
    HKEY key = nullptr;
    result.error = RegOpenKeyExW(HKEY_CURRENT_USER, PersonalizeKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (result.error) return result;
    Preference system, apps;
    result.error = ReadPreference(key, SystemValue, system);
    if (!result.error) result.error = ReadPreference(key, AppsValue, apps);
    const DWORD value = light ? 1 : 0;
    if (!result.error) {
        result.error = WritePreference(key, SystemValue, value);
        if (!result.error) {
            result.error = WritePreference(key, AppsValue, value);
            if (result.error) {
                result.rollbackError = system.exists ? WritePreference(key, SystemValue, system.value)
                    : RegDeleteValueW(key, SystemValue);
            }
        }
    }
    RegCloseKey(key);
    // Also notify if rollback failed, so observers reflect the actual partial state.
    if ((!result.error || result.rollbackError) && !QueueUserWorkItem(NotifyThemeChange, nullptr, WT_EXECUTEDEFAULT))
        result.notificationError = GetLastError();
    return result;
}
} // namespace acrylic

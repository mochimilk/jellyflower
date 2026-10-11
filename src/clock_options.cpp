// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock_options.hpp"
#include <shlobj.h>
#include <vector>
namespace acrylic {
namespace {
constexpr wchar_t ClockSettingsKey[] = L"Software\\AcrylicTaskbar";
DWORD Read(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, ClockSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
}
}
ClockOptions DecodeClockOptions(UINT value) {
    const UINT font = (value >> 8) & 0xFF;
    return {(value & 1) != 0, font <= 2 ? static_cast<ClockFont>(font) : ClockFont::System};
}
const wchar_t* ClockFontLabel(ClockFont font) {
    return font == ClockFont::Playground ? L"playground" : font == ClockFont::Pangaia ? L"pangia" : L"segoe (system)";
}
ClockOptions LoadClockOptions() {
    const DWORD font = Read(L"ClockFont", 0);
    return {Read(L"ClockDate", 1) != 0, font <= 2 ? static_cast<ClockFont>(font) : ClockFont::System};
}
DWORD SaveClockOption(const wchar_t* name, DWORD value) {
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, ClockSettingsKey, name, REG_DWORD, &value, sizeof(value)));
}
std::wstring ClockFontPath(ClockFont font) {
    if (font == ClockFont::System) return {};
    const wchar_t* setting = font == ClockFont::Playground ? L"ClockFontPlaygroundPath" : L"ClockFontPangaiaPath";
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, ClockSettingsKey, setting, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS
        && bytes > sizeof(wchar_t) && bytes < 65536) {
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
        if (RegGetValueW(HKEY_CURRENT_USER, ClockSettingsKey, setting, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes) == ERROR_SUCCESS)
            return buffer.data();
    }
    PWSTR downloads = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads))) return {};
    std::wstring path(downloads); CoTaskMemFree(downloads);
    return path + (font == ClockFont::Playground
        ? L"\\PP_Playground_-_Free_for_Personal_Use_v1.0\\PP Playground - Free for Personal Use v1.0\\otf\\PPPlayground-Light.otf"
        : L"\\PP_Pangaia_-_Free_for_Personal_Use_v1.0\\PP Pangaia - Free for Personal Use v1.0\\otf\\PPPangaia-Ultralight.otf");
}
}

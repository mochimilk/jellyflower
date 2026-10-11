// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock_options.hpp"
namespace acrylic {
namespace { constexpr wchar_t ClockSettingsKey[] = L"Software\\AcrylicTaskbar"; }
ClockOptions DecodeClockOptions(UINT value) {
    // Ignore retired font bits when upgrading an existing preference snapshot.
    return {(value & 1) != 0};
}
ClockOptions LoadClockOptions() {
    DWORD value = 1, bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, ClockSettingsKey, L"ClockDate", RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) value = 1;
    return {value != 0};
}
DWORD SaveClockDate(bool date) {
    const DWORD value = date ? 1 : 0;
    return static_cast<DWORD>(RegSetKeyValueW(HKEY_CURRENT_USER, ClockSettingsKey, L"ClockDate", REG_DWORD, &value, sizeof(value)));
}
}

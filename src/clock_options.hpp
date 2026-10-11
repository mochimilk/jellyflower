// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace acrylic {
enum class ClockFont : UINT { System, Playground, Pangaia };
struct ClockOptions {
    bool date = true;
    ClockFont font = ClockFont::System;
    UINT Packed() const { return (date ? 1u : 0u) | (static_cast<UINT>(font) << 8); }
};
ClockOptions DecodeClockOptions(UINT value);
const wchar_t* ClockFontLabel(ClockFont font);
std::wstring ClockFontPath(ClockFont font);
ClockOptions LoadClockOptions();
DWORD SaveClockOption(const wchar_t* name, DWORD value);
}

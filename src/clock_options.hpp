// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
struct ClockOptions {
    bool date = true;
    UINT Packed() const { return date ? 1u : 0u; }
};
ClockOptions DecodeClockOptions(UINT value);
ClockOptions LoadClockOptions();
DWORD SaveClockDate(bool date);
}

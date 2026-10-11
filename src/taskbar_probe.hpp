// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"

namespace acrylic {
struct TaskbarProbe {
    Backend backend;
    unsigned islands;
    bool retained;
};
// primary has already been verified as an Explorer taskbar. The process handle
// belongs to the bridge owner, so a reused PID cannot retain an old session.
TaskbarProbe ProbeTaskbar(DWORD build, HWND primary, HANDLE ownerProcess, DWORD ownerPid);
}

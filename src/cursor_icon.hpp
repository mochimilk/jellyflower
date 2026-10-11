// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// Draw the supplied SVG with the actual cursor's body/border colors.
// The caller owns the returned icon.
// A missing file falls back to Windows' current arrow.
HICON CreateCursorMenuIcon(const wchar_t* file, int pixels);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "bool_setting.hpp"

namespace acrylic {
// The dock's bar height in DIPs (the stock bar is 48): room for the pill to
// float with its icons centered and their highlights square.
inline constexpr LONG DockBarHeight = 64;
// Off by default: the stock full-width taskbar stays until the user asks for
// the floating dock.
inline constexpr BoolSetting DockSetting{L"Dock", false};
}

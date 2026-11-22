// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bool_setting.hpp"

namespace acrylic {
// Off by default: the touch keyboard keeps Windows' layout until the user
// chooses the symmetric one.
inline constexpr BoolSetting KeyboardLayoutSetting{L"KeyboardLayout", false};
}

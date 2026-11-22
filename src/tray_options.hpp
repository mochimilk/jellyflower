// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bool_setting.hpp"

namespace acrylic {
// Off by default: the stock notification area stays until the user asks for
// jellyflower's icon alone.
inline constexpr BoolSetting TrayOnlySetting{L"TrayOnly", false};
// Off by default: the top bar (top_bar.hpp) takes a strip from every display.
inline constexpr BoolSetting TopBarSetting{L"TopBar", false};
}

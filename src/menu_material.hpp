// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
namespace acrylic {
// XAML and DWM receive the same colors. Their different blur implementations
// mean equal inputs do not guarantee identical pixels.
constexpr DWORD MenuTint(bool light) {return light ? 0xB8F9F9F9u:0xB8202020u;}
constexpr COLORREF MenuBorder(bool light) {return light ? RGB(210,210,210):RGB(69,69,69);}
}

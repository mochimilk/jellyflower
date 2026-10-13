// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace acrylic {
inline constexpr const wchar_t* SearchPlaceholder(bool light) noexcept {
    return light ? L"searching for u \U0001F5A4" : L"searching for u \U0001F90D";
}
}

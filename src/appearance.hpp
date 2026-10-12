// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
enum class AcrylicMode : LONG { Off = 0, System = 1, Pure = 2 };
constexpr AcrylicMode NextAcrylicMode(AcrylicMode mode) noexcept {
    return mode == AcrylicMode::System ? AcrylicMode::Pure : mode == AcrylicMode::Pure ? AcrylicMode::Off : AcrylicMode::System;
}
constexpr AcrylicMode DecodeAcrylicMode(LONG value) noexcept {
    return value == 2 ? AcrylicMode::Pure : value == 1 ? AcrylicMode::System : AcrylicMode::Off;
}
constexpr const wchar_t* AcrylicLabel(AcrylicMode mode) noexcept {
    return mode == AcrylicMode::System ? L"acrylic (system)" : mode == AcrylicMode::Pure ? L"acrylic pure" : L"acrylic off";
}
constexpr const wchar_t* AcrylicModeName(AcrylicMode mode) noexcept {
    return mode == AcrylicMode::System ? L"system" : mode == AcrylicMode::Pure ? L"pure" : L"off";
}
struct AcrylicRecipe { double tint, luminosity; DWORD legacyColor; };
constexpr AcrylicRecipe Recipe(AcrylicMode mode, bool light) noexcept {
    // Color tint alone preserves backdrop brightness: white with zero
    // luminosity still looks black over black wallpaper. Set the brightness
    // layer explicitly in both themes. Legacy accents use ABGR alpha instead.
    // Pure keeps the required nonzero legacy alpha without adding XAML tint.
    return mode == AcrylicMode::Pure ? AcrylicRecipe{0.0, 0.0, 0x01000000u}
        : light ? AcrylicRecipe{0.48, 0.48, 0x7affffffu} : AcrylicRecipe{0.34, 0.72, 0xb8202020u};
}
} // namespace acrylic

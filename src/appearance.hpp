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
struct AcrylicRecipe { double tint, luminosity; bool automaticLuminosity; DWORD legacyColor; };
constexpr AcrylicRecipe Recipe(AcrylicMode mode, bool light) noexcept {
    // Legacy acrylic requires a nonzero alpha, as in upstream TranslucentTB.
    return mode == AcrylicMode::Pure ? AcrylicRecipe{0.0, 0.0, false, 0x01000000u}
        : light ? AcrylicRecipe{0.65, 0.0, false, 0xa6ffffffu} : AcrylicRecipe{0.40, 0.0, true, 0x66202020u};
}
} // namespace acrylic

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include <array>

namespace acrylic {
// The taskbar's fluted glass is drawn by the controller in its own windows: inside Explorer's process a
// host backdrop is a snapshot taken once, while the controller's stays live. The Explorer helper says
// where each glass goes through these slots: one per taskbar background, its taskbar window and the
// background's rectangle relative to that window, in pixels, with its corner radius. The helper writes
// a slot under glassSequence (odd while it writes); the controller reads until it sees the same even
// count before and after, keeps a glass window directly below each slot's taskbar window, and sets the
// slot's bit in glassShown. The helper clears a background's fill only while that bit is set.
inline constexpr size_t GlassSlotCount = 4;
static_assert(sizeof(SharedState::glassSlots) / sizeof(SharedState::glassSlots[0]) == GlassSlotCount, "SharedState holds every slot");
inline constexpr UINT GlassMessage = WM_APP + 110; // To the controller's window: the slots changed.

struct GlassSlot {
    HWND taskbar = nullptr;
    LONG left = 0, top = 0, width = 0, height = 0, corner = 0;
    bool Empty() const noexcept { return !taskbar || width <= 0 || height <= 0; }
    bool operator==(const GlassSlot& other) const noexcept {
        return taskbar == other.taskbar && left == other.left && top == other.top && width == other.width
            && height == other.height && corner == other.corner;
    }
    bool operator!=(const GlassSlot& other) const noexcept { return !(*this == other); }
};

// Window handles keep 32 significant bits, sign-extended, so a LONG carries one between processes.
inline LONG PackWindow(HWND window) noexcept { return static_cast<LONG>(reinterpret_cast<LONG_PTR>(window)); }
inline HWND UnpackWindow(LONG value) noexcept { return reinterpret_cast<HWND>(static_cast<LONG_PTR>(value)); }

inline void WriteGlassSlot(SharedState& state, size_t index, const GlassSlot& slot) noexcept {
    if (index >= GlassSlotCount) return;
    volatile LONG* fields = state.glassSlots[index];
    InterlockedIncrement(&state.glassSequence);
    InterlockedExchange(&fields[0], PackWindow(slot.taskbar));
    InterlockedExchange(&fields[1], slot.left);
    InterlockedExchange(&fields[2], slot.top);
    InterlockedExchange(&fields[3], slot.width);
    InterlockedExchange(&fields[4], slot.height);
    InterlockedExchange(&fields[5], slot.corner);
    InterlockedIncrement(&state.glassSequence);
}
// False while the helper keeps writing; the caller tries again on its next refresh.
inline bool ReadGlassSlots(SharedState& state, std::array<GlassSlot, GlassSlotCount>& slots) noexcept {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const LONG before = Read(state.glassSequence);
        if (before & 1) { SwitchToThread(); continue; }
        for (size_t i = 0; i < GlassSlotCount; ++i) {
            volatile LONG* fields = state.glassSlots[i];
            slots[i] = {UnpackWindow(Read(fields[0])), Read(fields[1]), Read(fields[2]), Read(fields[3]), Read(fields[4]), Read(fields[5])};
        }
        if (Read(state.glassSequence) == before) return true;
    }
    return false;
}
inline bool GlassShown(SharedState& state, size_t index) noexcept {
    return index < GlassSlotCount && ((Read(state.glassShown) >> index) & 1) != 0;
}

// Windows turns its own materials opaque to save energy, with transparency effects off and in high
// contrast; fluted glass follows it, and the caller falls back to Windows' acrylic, which does the same.
inline bool FlutedGlassAllowed() noexcept {
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1) return false;
    DWORD transparency = 1, bytes = sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &bytes);
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
    return transparency && !high;
}
}

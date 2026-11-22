// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cmath>
#include "compact.hpp"

namespace acrylic {
// The top bar: with the system tray's position set to top, every display gets
// a strip along its top edge, like a Mac's menu bar. The flower at its left
// opens the jellyflower menu, as the status bar's chevron does. At its right,
// a Quick Settings button and the clock open Windows' own panels; Windows
// anchors those to the taskbar, so they still open from its corner. The app
// icons stay in the taskbar's notification area: Explorer draws them inside
// the taskbar window. On a compact display the bar takes the status bar's
// place in the taller top margin. It is drawn like the status bar (compact.hpp):
// no fill, the same subtle buttons and ink.
inline constexpr int TopBarHeight = 32;       // DIP: Windows' caption height
inline constexpr float TopBarEdge = 8;        // DIP from the display's sides to the outer buttons
inline constexpr float TopBarGap = 4;         // DIP between the Quick Settings button and the clock
inline constexpr float TopBarIconSize = 16;   // DIP: the flower and the glyph, a tray icon's size
// Segoe Fluent Icons' Equalizer: two sliders, Windows' nearest glyph to a
// Mac's Control Center.
inline constexpr wchar_t QuickSettingsGlyph[] = L"";
inline constexpr wchar_t QuickSettingsUri[] = L"ms-actioncenter:controlcenter/&showFooter=true";
inline constexpr wchar_t NotificationsUri[] = L"ms-actioncenter:";

// The bar's buttons. The status bar has only the menu button.
enum class BarButton : int { None = -1, Menu, QuickSettings, Clock, Count };

// The top edge's thickness: the compact top margin, or the top bar.
inline int TopEdgePixels(bool compact, UINT dpi) noexcept {
    return compact ? CompactEdgePixels(ABE_TOP, dpi) : MulDiv(TopBarHeight, static_cast<int>(dpi), 96);
}

// Window coordinates of each button, indexed by BarButton; empty when the bar
// is too narrow for it. Buttons are the status bar's: inset from the top and
// bottom, padded at the sides. The flower and the clock sit TopBarEdge in, so
// on a compact display they line up with the windows in the side margins.
struct TopBarLayout { RECT buttons[static_cast<int>(BarButton::Count)]{}; };
inline TopBarLayout LayoutTopBar(int width, int height, float scale, float clockWidth) noexcept {
    const auto px = [scale](float dip) { return static_cast<LONG>(std::lround(dip * scale)); };
    const LONG top = px(StatusButtonInset), bottom = height - top, edge = px(TopBarEdge);
    const LONG icon = px(2 * StatusButtonPadding + TopBarIconSize);
    const RECT menu{edge, top, edge + icon, bottom};
    const LONG clockRight = width - edge, clockLeft = clockRight - static_cast<LONG>(std::ceil(clockWidth)) - 2 * px(StatusButtonPadding);
    const RECT clock{clockLeft, top, clockRight, bottom};
    const RECT quickSettings{clockLeft - px(TopBarGap) - icon, top, clockLeft - px(TopBarGap), bottom};
    TopBarLayout layout;
    layout.buttons[static_cast<int>(BarButton::Menu)] = menu;
    // A bar too narrow for everything drops Quick Settings, then the clock.
    if (clock.left >= menu.right) layout.buttons[static_cast<int>(BarButton::Clock)] = clock;
    if (quickSettings.left >= menu.right) layout.buttons[static_cast<int>(BarButton::QuickSettings)] = quickSettings;
    return layout;
}
} // namespace acrylic

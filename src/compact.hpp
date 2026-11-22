// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <shellapi.h>
#include "bool_setting.hpp"

namespace acrylic {
// Compact displays are the ones too narrow for Start's native menu: the small
// layout's 642-DIP column plus the 12 DIP of frame padding on each side, the
// same test start_frame.hpp's narrow layout makes inside Start. A portrait
// tablet at 300% (626 DIP) is compact; anything 666 DIP or wider is not.
//
// On a compact display the controller reserves each screen edge as an app bar
// (compact_screen.cpp). Windows then maximizes and snaps every window inside
// the margins, whatever draws its title bar, so title bars clear the display's
// rounded corners without any change to the apps. The top margin is taller
// and can show the time: the status bar.
inline constexpr double CompactStartWidth = 642 + 2 * 12; // DIP
inline constexpr int CompactSideMargin = 8;  // DIP: left, right and above the taskbar
inline constexpr int CompactTopInset = 40;   // DIP: the status bar, or the top margin without it
inline constexpr float StatusBarTextSize = 14; // DIP

inline bool IsCompactDisplay(LONG widthPixels, UINT dpi) noexcept {
    return widthPixels > 0 && dpi > 0 && widthPixels * 96.0 / dpi < CompactStartWidth - .01;
}
inline int CompactEdgePixels(UINT edge, UINT dpi) noexcept {
    return MulDiv(edge == ABE_TOP ? CompactTopInset : CompactSideMargin, static_cast<int>(dpi), 96);
}
// The rectangle proposed to ABM_QUERYPOS: a strip along the monitor's edge.
inline RECT AppBarProposal(UINT edge, RECT monitor, int thickness) noexcept {
    if (edge == ABE_TOP) monitor.bottom = monitor.top + thickness;
    else if (edge == ABE_BOTTOM) monitor.top = monitor.bottom - thickness;
    else if (edge == ABE_LEFT) monitor.right = monitor.left + thickness;
    else monitor.left = monitor.right - thickness;
    return monitor;
}
// ABM_QUERYPOS moves the strip's outer edge clear of other app bars (the
// bottom one ends up above the taskbar). Keep that edge, restore the thickness.
inline RECT AppBarPlacement(UINT edge, RECT queried, int thickness) noexcept {
    if (edge == ABE_TOP) queried.bottom = queried.top + thickness;
    else if (edge == ABE_BOTTOM) queried.top = queried.bottom - thickness;
    else if (edge == ABE_LEFT) queried.right = queried.left + thickness;
    else queried.left = queried.right - thickness;
    return queried;
}
// A window covering the whole monitor (a game, a video, F11) hides the status
// bar. A maximized window never does: it stops at the reserved top margin.
inline bool CoversMonitor(const RECT& window, const RECT& monitor) noexcept {
    return window.left <= monitor.left && window.top <= monitor.top
        && window.right >= monitor.right && window.bottom >= monitor.bottom;
}
// Milliseconds until just after the next minute starts.
inline UINT NextMinuteDelay(const SYSTEMTIME& now) noexcept {
    const UINT elapsed = (now.wSecond % 60) * 1000u + now.wMilliseconds % 1000u;
    return 60000 - elapsed + 50;
}
// Windows 11's TextFillColorPrimary, as ARGB: black at 0xE4 alpha on light,
// white on dark. The status bar has no fill, so it follows the desktop's theme.
inline DWORD StatusBarInk(bool light) noexcept { return light ? 0xE4000000u : 0xFFFFFFFFu; }

// The time and its chevron form one subtle button that opens the jellyflower
// menu. Its fill follows Windows 11's subtle button tokens: none at rest,
// SubtleFillColorSecondary on hover (and while its menu is open) and
// SubtleFillColorTertiary with TextFillColorSecondary ink when pressed. At
// rest it keeps an alpha of 1, invisible but enough for the layered window
// to take clicks there, while every other pixel of the bar lets them through.
enum class StatusButton { Rest, Hover, Pressed, Open };
inline DWORD StatusButtonFill(bool light, StatusButton state) noexcept {
    if (state == StatusButton::Rest) return 0x01000000u;
    if (state == StatusButton::Pressed) return light ? 0x06000000u : 0x0AFFFFFFu;
    return light ? 0x09000000u : 0x0FFFFFFFu;
}
inline DWORD StatusButtonInk(bool light, StatusButton state) noexcept {
    if (state == StatusButton::Pressed) return light ? 0x9E000000u : 0xC5FFFFFFu;
    return StatusBarInk(light);
}
// DIP. The chevron sits right of the time; the time stays centered.
inline constexpr float StatusChevronGap = 6, StatusChevronWidth = 8, StatusChevronHeight = 4, StatusChevronStroke = 1.5f;
inline constexpr float StatusButtonPadding = 8, StatusButtonInset = 4, StatusButtonRadius = 4;

// On by default: compact displays show the time in the top margin.
inline constexpr BoolSetting StatusBarSetting{L"StatusBar", true};
} // namespace acrylic

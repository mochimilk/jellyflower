// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "appearance.hpp"

namespace acrylic {
void RefreshLanguageMenu(HWND owner);
void RefreshUpdateMenu(HWND owner);
void RefreshCursorMenu(HWND owner);
void RefreshThemeMenu(HWND owner);
void RefreshWallpaperMenu(HWND owner);
void RefreshTaskbarMenu(HWND owner);
// trayBounds is the button that opened the menu: pressing it again closes the
// menu instead of dismissing and reopening it. below opens the menu downward,
// centered on the anchor, for a menu button at the top of the screen (the top
// bar's flower or the status bar's time).
UINT ShowTrayMenu(HWND owner, POINT anchor, UINT dpi, AcrylicMode mode, bool light, UINT cursorMode = 0, const RECT& trayBounds = {}, bool below = false);
} // namespace acrylic

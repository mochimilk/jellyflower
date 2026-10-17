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
UINT ShowTrayMenu(HWND owner, POINT anchor, UINT dpi, AcrylicMode mode, bool light, UINT cursorMode = 0, const RECT& trayBounds = {});
} // namespace acrylic

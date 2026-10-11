// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace acrylic {
// Windows 11's body/control text is 14 effective pixels, regular Segoe UI
// Variable. GDI exposes its optical text face under this installed family name.
inline HFONT CreateSystemMenuFont(UINT dpi) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    LOGFONTW font{};
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
        font = metrics.lfMenuFont;
    else wcscpy_s(font.lfFaceName, L"Segoe UI");
    const HDC dc = GetDC(nullptr);
    if (dc) {
        LOGFONTW query{}; query.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(query.lfFaceName, L"Segoe UI Variable Text");
        bool installed = false;
        EnumFontFamiliesExW(dc, &query, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM value)->int {
            *reinterpret_cast<bool*>(value) = true; return 0;
        }, reinterpret_cast<LPARAM>(&installed), 0);
        if (installed) wcscpy_s(font.lfFaceName, query.lfFaceName);
        ReleaseDC(nullptr, dc);
    }
    font.lfHeight = -MulDiv(14, static_cast<int>(dpi), 96);
    font.lfWidth = font.lfEscapement = font.lfOrientation = 0;
    font.lfWeight = FW_NORMAL;
    font.lfItalic = font.lfUnderline = font.lfStrikeOut = FALSE;
    // Subpixel ClearType is inappropriate on a translucent ARGB surface.
    font.lfQuality = ANTIALIASED_QUALITY;
    return CreateFontIndirectW(&font);
}
constexpr int MenuEdgeGap = 12;
constexpr ULONGLONG MenuEntranceDuration = 250;
constexpr int MenuEntranceDistance = 32;
inline bool MenuAnimationsEnabled() {
    BOOL client = FALSE, menus = FALSE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client, 0) && client
        && SystemParametersInfoW(SPI_GETMENUANIMATION, 0, &menus, 0) && menus;
}
inline double MenuEntranceProgress(double elapsed) {
    // Microsoft's Direct Entrance cubic-bezier(0,0,0,1): x=t^3,
    // y=3*t^2-2*t^3. Invert x, rather than treating time as the curve parameter.
    const double x = std::clamp(elapsed, 0.0, 1.0);
    const double t = std::cbrt(x);
    return 3*t*t - 2*x;
}
}

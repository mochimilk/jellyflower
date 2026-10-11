// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace acrylic {
using MenuMetricsQuery = decltype(&SystemParametersInfoForDpi);
// Inherit the complete Windows menu font, including family, size and weight.
// An empty family would invoke GDI font mapping instead of menu inheritance.
inline HFONT CreateSystemMenuFont(UINT dpi, MenuMetricsQuery query = SystemParametersInfoForDpi) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    LOGFONTW font{};
    if(query(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi)) font=metrics.lfMenuFont;
    else if(!GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(font),&font)) return nullptr;
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

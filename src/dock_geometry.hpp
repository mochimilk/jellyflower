// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace acrylic {
// Floating dock geometry in DIPs. The stock buttons inset their hover
// highlight 4 DIPs from the bar's top and bottom and draw the running
// indicator inside it, so a 3-DIP gap keeps both inside the pill. Corners are
// 3/8 of the pill's height: heavy, but short of fully round ends. 12 DIPs of
// side padding keeps the outer highlights well inside them.
inline constexpr double DockGap = 3, DockPadding = 12, DockCornerRatio = 0.375;
// Each button's hover and running highlight takes an app icon's corners:
// 22.5% of its side, 9 DIPs on the stock 40-DIP square.
inline constexpr double DockHighlightCorner = 0.225;

struct DockBox {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool Empty() const noexcept { return right <= left || bottom <= top; }
    bool operator==(const DockBox& other) const noexcept {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }
    bool operator!=(const DockBox& other) const noexcept { return !(*this == other); }
};
// The pill around the app group, inside a taskbar background of this size.
// Empty when there is no group or the bar is too short to float in.
inline DockBox DockPill(const DockBox& group, double width, double height) noexcept {
    if (group.Empty() || width <= 0 || height <= 2 * DockGap + 1) return {};
    const DockBox pill{(std::max)(0.0, group.left - DockPadding), DockGap,
        (std::min)(width, group.right + DockPadding), height - DockGap};
    return pill.Empty() ? DockBox{} : pill;
}
// A heavy, squircle-like corner: 3/8 of the pill's height, which keeps a
// short straight side between the arcs instead of fully round ends.
inline double DockCorner(const DockBox& pill) noexcept {
    return (std::max)(0.0, (std::min)((pill.bottom - pill.top) * DockCornerRatio, (pill.right - pill.left) / 2));
}
// Window-region rectangles in physical pixels, relative to the taskbar
// window. Hits are XAML-root DIPs; offset is the island's position in the
// window. The first hit (the pill) grows by a pixel so its antialiased edge
// is never clipped. With auto hide, the window's top rows stay in the region:
// they are what remains on screen while the taskbar is hidden.
inline std::vector<RECT> DockRegionRects(const std::vector<DockBox>& hits, double scale, POINT offset,
    LONG width, LONG height, bool autoHide) {
    std::vector<RECT> rects;
    if (scale <= 0 || width <= 0 || height <= 0) return rects;
    for (size_t i = 0; i < hits.size(); ++i) {
        const auto& hit = hits[i];
        if (hit.Empty()) continue;
        const LONG grow = i == 0 ? 1 : 0;
        RECT rect{static_cast<LONG>(std::floor(hit.left * scale)) + offset.x - grow, static_cast<LONG>(std::floor(hit.top * scale)) + offset.y - grow,
            static_cast<LONG>(std::ceil(hit.right * scale)) + offset.x + grow, static_cast<LONG>(std::ceil(hit.bottom * scale)) + offset.y + grow};
        rect.left = (std::max)(rect.left, 0L);
        rect.top = (std::max)(rect.top, 0L);
        rect.right = (std::min)(rect.right, width);
        rect.bottom = (std::min)(rect.bottom, height);
        if (rect.right > rect.left && rect.bottom > rect.top) rects.push_back(rect);
    }
    if (autoHide && !rects.empty())
        rects.push_back({0, 0, width, (std::min)(height, (std::max)(2L, static_cast<LONG>(std::ceil(scale))))});
    return rects;
}
}

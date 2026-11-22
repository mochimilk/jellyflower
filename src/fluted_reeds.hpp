// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace acrylic {
// Fluted glass, after willow-ui's, at the reed width of its small surfaces. willow ray traces
// each reed into a map; here each reed is one lens that shows c - k (x - c) around its center c, flipped
// and squeezed, with blue bent further than red, and k is the map's average over a reed.
inline constexpr double ReedWidth = 14, RedBend = 2.24, BlueBend = 3.86;
// The share of the light already there that each reed reflects across its width, most on its left flank.
inline constexpr std::pair<float, float> ReedSheen[]{{0.f, .055f}, {.06f, .035f}, {.25f, .13f}, {.45f, .035f}, {.9f, .033f}, {1.f, .055f}};

struct Reed { double left, width, red, blue; };

// Reeds of about ReedWidth DIPs that fill the width exactly. Near the edge a reed bends less, so it never
// reads past the glass: the pane thins toward its rim.
inline std::vector<Reed> FlutedReeds(double width, unsigned dpi) {
    std::vector<Reed> reeds;
    if (!(width > 0) || !dpi) return reeds;
    const int count = std::max(1, static_cast<int>(std::lround(width * 96 / (ReedWidth * dpi))));
    const double reed = width / count;
    for (int i = 0; i < count; ++i) {
        const double left = i * reed, center = left + reed / 2, room = 2 * std::min(center, width - center) / reed;
        reeds.push_back({left, reed, std::min(RedBend, room), std::min(BlueBend, room)});
    }
    return reeds;
}
}

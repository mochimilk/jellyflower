// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dock_geometry.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using acrylic::DockBox;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(double a, double b) { return std::fabs(a - b) < 0.01; }
bool Near(const DockBox& a, const DockBox& b) { return Near(a.left, b.left) && Near(a.top, b.top) && Near(a.right, b.right) && Near(a.bottom, b.bottom); }
void Pill() {
    Check(acrylic::DockPill({}, 800, 48).Empty(), "no group, no pill");
    Check(acrylic::DockPill({300, 0, 480, 48}, 800, 6).Empty(), "a bar too short to float in stays stock");
    Check(Near(acrylic::DockPill({300, 0, 480, 48}, 800, 48), {288, 3, 492, 45}), "pill pads the group and floats 3 DIPs from both edges");
    Check(Near(acrylic::DockPill({300, 0, 480, 64}, 800, 64), {288, 3, 492, 61}), "the taller bar's pill floats the same 3 DIPs");
    Check(Near(acrylic::DockPill({0, 0, 120, 48}, 800, 48), {0, 3, 132, 45}), "left-aligned group stays on screen");
    Check(Near(acrylic::DockPill({700, 0, 800, 48}, 800, 48), {688, 3, 800, 45}), "right edge stays on screen");
    Check(Near(acrylic::DockCorner({0, 3, 300, 45}), 15.75) && Near(acrylic::DockCorner({0, 3, 300, 23}), 7.5) && Near(acrylic::DockCorner({0, 0, 8, 40}), 4),
        "corners are 3/8 of the pill's height, never wider than the pill");
    // The outer hover highlight's top-left arc (40x40 from the bar's top 4
    // DIPs, 2 DIPs in from its button, corner radius 4) stays inside the
    // pill's corner arc.
    const DockBox pill = acrylic::DockPill({300, 0, 480, 48}, 800, 48);
    const double r = acrylic::DockCorner(pill), cx = pill.left + r, cy = pill.top + r;
    const double corner = 4, hx = 300 + 2 + corner, hy = 4 + corner;
    Check(hx >= cx || hy >= cy || std::hypot(hx - cx, hy - cy) + corner <= r, "outer hover highlight fits inside the pill's corner");
    Check(Near(std::round(40 * acrylic::DockHighlightCorner), 9), "highlights take an app icon's 9-DIP corners on the 40-DIP square");
}
void Region() {
    const std::vector<DockBox> hits{{10.2, 3, 100.4, 45}, {200, 0, 260, 48}};
    auto rects = acrylic::DockRegionRects(hits, 1.5, {0, 0}, 400, 72, false);
    Check(rects.size() == 2, "one region rectangle per hit");
    const RECT pill{14, 3, 152, 69}, tray{300, 0, 390, 72};
    Check(EqualRect(&rects[0], &pill) && EqualRect(&rects[1], &tray), "DIPs scale outward to whole pixels; the pill grows a pixel");
    rects = acrylic::DockRegionRects(hits, 1.5, {5, -2}, 400, 72, true);
    const RECT shifted{19, 1, 157, 67}, strip{0, 0, 400, 2};
    Check(rects.size() == 3 && EqualRect(&rects[0], &shifted) && EqualRect(&rects[2], &strip), "island offset applied; auto hide keeps the top rows that stay on screen");
    rects = acrylic::DockRegionRects({{350, 0, 900, 60}}, 1, {0, 0}, 400, 48, false);
    const RECT clamped{349, 0, 400, 48};
    Check(rects.size() == 1 && EqualRect(&rects[0], &clamped), "region clamped to the window");
    Check(acrylic::DockRegionRects({}, 1, {0, 0}, 400, 48, true).empty(), "no hits, no region, even with auto hide");
    Check(acrylic::DockRegionRects({{0, 0, 10, 10}}, 0, {0, 0}, 400, 48, false).empty(), "no scale, no region");
}
int main() {
    try {
        Pill();
        Region();
        std::puts("PASS: dock pill padding, float gap and clamping; 3/8 corners; highlights inside the corner arc; app-icon highlight corners; region rectangles, offset, auto-hide strip and clamping.");
        return 0;
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// The top bar's geometry: its height on ordinary and compact displays, and
// where the flower, the Quick Settings button and the clock go. No desktop.
#include "../src/top_bar.hpp"
#include <cstdio>
#include <stdexcept>

using namespace acrylic;

namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
bool Same(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
const RECT& Button(const TopBarLayout& layout, BarButton button) { return layout.buttons[static_cast<int>(button)]; }
bool Inside(const RECT& button, int width, int height) {
    return button.left >= 0 && button.top >= 0 && button.right <= width && button.bottom <= height && button.left < button.right && button.top < button.bottom;
}
} // namespace

int main() {
    try {
        Check(TopEdgePixels(false, 96) == 32 && TopEdgePixels(false, 144) == 48 && TopEdgePixels(false, 168) == 56, "the top bar is 32 DIP on ordinary displays");
        Check(TopEdgePixels(true, 288) == 120 && TopEdgePixels(true, 96) == CompactEdgePixels(ABE_TOP, 96), "a compact display keeps its 40-DIP top margin");

        // A 1920-pixel desktop at 100% with a 120-pixel clock.
        const auto desktop = LayoutTopBar(1920, 32, 1, 120);
        Check(Same(Button(desktop, BarButton::Menu), {8, 4, 40, 28}), "the flower is 8 DIP in, a 16-DIP icon padded like the status bar's button");
        Check(Same(Button(desktop, BarButton::Clock), {1776, 4, 1912, 28}), "the clock ends 8 DIP from the right edge, padded around its text");
        Check(Same(Button(desktop, BarButton::QuickSettings), {1740, 4, 1772, 28}), "Quick Settings sits 4 DIP left of the clock");

        // The portrait tablet at 300% (compact): the bar is the 120-pixel top margin.
        const auto tablet = LayoutTopBar(1878, 120, 3, 420.4f);
        Check(Same(Button(tablet, BarButton::Menu), {24, 12, 120, 108}), "at 300% the flower lines up with the 24-pixel side margin");
        Check(Same(Button(tablet, BarButton::Clock), {1854 - 421 - 48, 12, 1854, 108}), "the clock rounds its text width up");
        Check(Button(tablet, BarButton::QuickSettings).right == Button(tablet, BarButton::Clock).left - 12, "the gap scales too");

        const struct { const TopBarLayout& layout; int width, height; } bars[]{{desktop, 1920, 32}, {tablet, 1878, 120}};
        for (const auto& [layout, width, height] : bars) {
            for (const auto button : {BarButton::Menu, BarButton::QuickSettings, BarButton::Clock})
                Check(Inside(Button(layout, button), width, height), "every button lies inside the bar");
            Check(Button(layout, BarButton::Menu).right < Button(layout, BarButton::QuickSettings).left
                && Button(layout, BarButton::QuickSettings).right < Button(layout, BarButton::Clock).left, "buttons run left to right without overlapping");
            Check(Button(layout, BarButton::Menu).top == Button(layout, BarButton::Clock).top
                && Button(layout, BarButton::Menu).bottom == Button(layout, BarButton::Clock).bottom, "buttons share one row");
        }

        // Too narrow for everything: Quick Settings goes first, then the clock.
        const auto narrow = LayoutTopBar(200, 32, 1, 120);
        Check(IsRectEmpty(&Button(narrow, BarButton::QuickSettings)) && !IsRectEmpty(&Button(narrow, BarButton::Clock)), "a narrow bar drops Quick Settings first");
        const auto tiny = LayoutTopBar(120, 32, 1, 120);
        Check(IsRectEmpty(&Button(tiny, BarButton::QuickSettings)) && IsRectEmpty(&Button(tiny, BarButton::Clock))
            && Same(Button(tiny, BarButton::Menu), {8, 4, 40, 28}), "the menu button always stays");
        std::puts("TopBarLayout passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TopBarLayout failed: %s\n", error.what());
        return 1;
    }
}

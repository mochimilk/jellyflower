// SPDX-License-Identifier: GPL-3.0-or-later
// Compact display geometry: which displays get margins, the app bar strips
// along their edges, the status bar's clock timing and ink. No shell or desktop.
#include "../src/compact.hpp"
#include <cstdio>
#include <stdexcept>

using namespace acrylic;

namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
bool Same(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
SYSTEMTIME At(WORD second, WORD milliseconds) {
    SYSTEMTIME time{};
    time.wHour = 9; time.wMinute = 41; time.wSecond = second; time.wMilliseconds = milliseconds;
    return time;
}
} // namespace

int main() {
    try {
        // Start's narrow layout starts below 642 + 2 x 12 DIP (start_frame.hpp).
        Check(IsCompactDisplay(1878, 288), "a portrait tablet at 300% (626 DIP) is compact");
        Check(!IsCompactDisplay(2670, 288), "the same tablet in landscape (890 DIP) is not");
        Check(IsCompactDisplay(665, 96) && !IsCompactDisplay(666, 96), "the threshold is Start's menu plus its frame padding");
        Check(!IsCompactDisplay(1920, 96) && !IsCompactDisplay(2560, 240) && !IsCompactDisplay(2560, 192), "ordinary desktops and laptops are not compact");
        Check(IsCompactDisplay(1280, 192), "a 1280-pixel display at 200% (640 DIP) is too narrow for Start, so it is compact");
        Check(IsCompactDisplay(1080, 168), "a 1080-pixel-wide portrait phone at 175% (617 DIP) is compact");
        Check(!IsCompactDisplay(0, 96) && !IsCompactDisplay(600, 0) && !IsCompactDisplay(-5, 96), "missing sizes are never compact");

        Check(CompactEdgePixels(ABE_TOP, 288) == 120 && CompactEdgePixels(ABE_LEFT, 288) == 24, "at 300% the top inset is 120 px and the margins 24 px");
        Check(CompactEdgePixels(ABE_RIGHT, 96) == 8 && CompactEdgePixels(ABE_BOTTOM, 96) == 8 && CompactEdgePixels(ABE_TOP, 96) == 40, "margins are 8 DIP and the top inset 40 DIP");
        Check(CompactEdgePixels(ABE_TOP, 168) == 70 && CompactEdgePixels(ABE_LEFT, 168) == 14, "fractional scales round to whole pixels");

        const RECT monitor{0, 0, 1878, 2670};
        Check(Same(AppBarProposal(ABE_TOP, monitor, 96), {0, 0, 1878, 96}), "the top strip spans the display's top edge");
        Check(Same(AppBarProposal(ABE_BOTTOM, monitor, 24), {0, 2646, 1878, 2670}), "the bottom strip spans the display's bottom edge");
        Check(Same(AppBarProposal(ABE_LEFT, monitor, 24), {0, 0, 24, 2670}) && Same(AppBarProposal(ABE_RIGHT, monitor, 24), {1854, 0, 1878, 2670}), "side strips run the full height");
        const RECT second{-1878, 100, 0, 2770};
        Check(Same(AppBarProposal(ABE_TOP, second, 96), {-1878, 100, 0, 196}) && Same(AppBarProposal(ABE_RIGHT, second, 24), {-24, 100, 0, 2770}), "a display left of the primary keeps its negative coordinates");

        // ABM_QUERYPOS lifts the bottom strip above a 144-px taskbar and moves both edges.
        Check(Same(AppBarPlacement(ABE_BOTTOM, {0, 2502, 1878, 2526}, 24), {0, 2502, 1878, 2526}), "an adjusted bottom strip keeps its thickness above the taskbar");
        Check(Same(AppBarPlacement(ABE_BOTTOM, {0, 2646, 1878, 2526}, 24), {0, 2502, 1878, 2526}), "only the outer edge of a query result counts");
        Check(Same(AppBarPlacement(ABE_TOP, {0, 0, 1878, 0}, 96), {0, 0, 1878, 96}), "the top strip is rebuilt from its top edge");
        Check(Same(AppBarPlacement(ABE_LEFT, {10, 0, 10, 2670}, 24), {10, 0, 34, 2670}) && Same(AppBarPlacement(ABE_RIGHT, {0, 0, 1860, 2670}, 24), {1836, 0, 1860, 2670}), "side strips are rebuilt from their outer edges");

        Check(CoversMonitor({0, 0, 1878, 2670}, monitor) && CoversMonitor({-8, -8, 1886, 2678}, monitor), "a full-screen window covers the display");
        // A maximized window's invisible borders reach the side edges, but it
        // starts below the reserved top inset.
        Check(!CoversMonitor({0, 96, 1878, 2526}, monitor), "a maximized window does not count as full-screen");
        Check(!CoversMonitor({0, 0, 1878, 2669}, monitor) && !CoversMonitor({1878, 0, 3756, 2670}, monitor), "a window short of an edge, or on another display, does not cover it");

        Check(NextMinuteDelay(At(0, 0)) == 60050 && NextMinuteDelay(At(59, 999)) == 51 && NextMinuteDelay(At(30, 500)) == 29550, "the clock wakes just after each minute starts");
        Check(NextMinuteDelay(At(60, 0)) == 60050, "a leap second still waits for the next minute");

        Check(StatusBarInk(true) == 0xE4000000u && StatusBarInk(false) == 0xFFFFFFFFu, "the status bar uses TextFillColorPrimary for the desktop's theme");
        // The menu button: invisible at rest but still hit-testable, subtle fills otherwise.
        for (const bool light : {true, false}) {
            Check(StatusButtonFill(light, StatusButton::Rest) >> 24 == 1, "at rest the button keeps alpha 1 so the layered window takes its clicks");
            Check(StatusButtonFill(light, StatusButton::Hover) == StatusButtonFill(light, StatusButton::Open), "an open menu keeps the hover fill");
            Check(StatusButtonInk(light, StatusButton::Rest) == StatusBarInk(light) && StatusButtonInk(light, StatusButton::Open) == StatusBarInk(light), "the time keeps its ink unless pressed");
        }
        Check(StatusButtonFill(true, StatusButton::Hover) == 0x09000000u && StatusButtonFill(false, StatusButton::Hover) == 0x0FFFFFFFu, "hover is SubtleFillColorSecondary");
        Check(StatusButtonFill(true, StatusButton::Pressed) == 0x06000000u && StatusButtonFill(false, StatusButton::Pressed) == 0x0AFFFFFFu, "pressed is SubtleFillColorTertiary");
        Check(StatusButtonInk(true, StatusButton::Pressed) == 0x9E000000u && StatusButtonInk(false, StatusButton::Pressed) == 0xC5FFFFFFu, "pressed ink is TextFillColorSecondary");
        std::puts("CompactLogic passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CompactLogic failed: %s\n", error.what());
        return 1;
    }
}

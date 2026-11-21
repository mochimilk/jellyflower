// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace acrylic::StartMetrics {
// Effective pixels (DIP), shared by every Start view. Content widths are derived
// from the current viewport; only spacing, icons and minimum targets are tokens.
inline constexpr double SectionInset = 32;
inline constexpr double RowPadding = 12;
inline constexpr double RowInset = SectionInset - RowPadding;
inline constexpr double GridGap = 16;
inline constexpr double HeadingTop = 28;
inline constexpr double PinnedRadius = 12;
inline constexpr double PinnedRowGap = 12;
inline constexpr double FooterHeightAddition = 16;
inline constexpr double FooterActionRadius = 12;
inline constexpr double SearchIcon = 20;
inline constexpr double SearchGap = 8;
inline constexpr double SearchResultsGap = 12;
inline constexpr double SurfaceRadius = 20;
inline constexpr double UserAvatar = 28;
inline constexpr double UserBadgeHeight = 40;
inline constexpr double UserBadgeRadius = 10;
inline constexpr double UserBadgeVerticalReduction = 4;
// Narrow layout, for displays too narrow for Start's native width (a tall,
// small tablet at a high scale). The menu takes the display's width and the
// pinned list spreads four columns across it. Targets fill their column but
// stay 80 DIP tall, so wide columns don't make tall rows. The inset matches the
// pinned list's 24-DIP indicator column, so the grid sits evenly in the menu.
inline constexpr unsigned NarrowPinnedColumns = 4;
inline constexpr double NarrowPinnedHeight = 80;
inline constexpr double NarrowPinnedInset = 24;
inline constexpr double NarrowPinnedRowGap = 8;
}

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
inline constexpr double PinnedHeight = 64;
inline constexpr double PinnedRowGap = 12;
inline constexpr double SearchIcon = 20;
inline constexpr double SearchGap = 8;
inline constexpr double SearchResultsGap = 12;
inline constexpr double SurfaceRadius = 20;
inline constexpr double UserAvatar = 28;
inline constexpr double UserBadgeHeight = 40;
inline constexpr double UserBadgeRadius = 10;
}

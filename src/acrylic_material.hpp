// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "appearance.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace acrylic {
inline void ConfigureAcrylic(const winrt::Windows::UI::Xaml::Media::AcrylicBrush& brush, AcrylicMode mode, bool light) {
    const auto recipe = Recipe(mode, light);
    brush.TintColor(light ? winrt::Windows::UI::Color{255, 255, 255, 255} : winrt::Windows::UI::Color{255, 32, 32, 32});
    brush.TintOpacity(recipe.tint);
    brush.TintLuminosityOpacity(winrt::box_value(recipe.luminosity).as<winrt::Windows::Foundation::IReference<double>>());
    brush.FallbackColor(light ? winrt::Windows::UI::Color{255, 243, 243, 243} : winrt::Windows::UI::Color{255, 32, 32, 32});
}
}

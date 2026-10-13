// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/acrylic_material.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double Linear(double srgb) { return srgb <= .04045 ? srgb / 12.92 : std::pow((srgb + .055) / 1.055, 2.4); }
double Contrast(double backdrop, bool light) {
    const double luminance = Linear(backdrop);
    return light ? (luminance + .05) / .05 : 1.05 / (luminance + .05);
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        {
            winrt::Windows::UI::Xaml::Media::AcrylicBrush brush;
            // Exercise the actual production setter on a live XAML brush,
            // including pure -> system and theme changes on the same object.
            for (int cycle = 0; cycle < 3; ++cycle) for (bool light : {true, false}) {
                acrylic::ConfigureAcrylic(brush, acrylic::AcrylicMode::Pure, light);
                Check(brush.TintOpacity() == 0 && brush.TintLuminosityOpacity()
                    && brush.TintLuminosityOpacity().Value() == 0, "pure stays untinted in both themes");
                acrylic::ConfigureAcrylic(brush, acrylic::AcrylicMode::System, light);
                const auto luminosity = brush.TintLuminosityOpacity();
                Check(luminosity && luminosity.Value() > 0 && luminosity.Value() <= 1, "system has explicit brightness layer");
                Check(1-luminosity.Value() >= .25, "system brightness layer leaves at least one quarter of the backdrop contribution");
                const auto color = brush.TintColor(), fallback = brush.FallbackColor();
                Check(color.R == color.G && color.G == color.B && fallback.A == 255, "neutral theme and opaque fallback");
                const auto recipe = acrylic::Recipe(acrylic::AcrylicMode::System, light);
                for (double background : {0.0, 1.0}) {
                    // Achromatic extremes reduce the luminosity blend to this
                    // brightness interpolation. This is a contrast model,
                    // not a measurement of Windows' GPU/compositor output.
                    const double modeled = background * (1 - luminosity.Value()) + color.R / 255.0 * luminosity.Value();
                    const double alpha = (recipe.legacyColor >> 24) / 255.0;
                    const double legacy = background * (1 - alpha) + (recipe.legacyColor & 255) / 255.0 * alpha;
                    if (light && background == 0) {
                        // The requested lighter white wash trades away the old
                        // normal-text contrast floor over fully black wallpaper.
                        // Keep that limitation visible rather than claiming AA.
                        Check(modeled > 0 && modeled < .48 && legacy > 0 && legacy < .48,
                            "light theme shows more black backdrop than the previous material");
                        Check(Contrast(modeled, light) < 4.5 && Contrast(legacy, light) < 4.5,
                            "transparent light material has documented black-backdrop contrast limit");
                    } else {
                        Check(Contrast(modeled, light) >= 4.5, "dark material and light-on-white keep modeled text contrast");
                        Check(Contrast(legacy, light) >= 4.5, "legacy dark and light-on-white keep modeled text contrast");
                    }
                }
                Check(Contrast(fallback.R / 255.0, light) >= 4.5, "system fallback stays readable");
            }
            Check(Contrast(0, true) < 4.5, "regression model rejects previous zero-luminosity white on black");
        }
        manager.Close();
        std::puts("PASS: real AcrylicBrush theme/pure transitions, lighter white wash, explicit brightness, documented black-backdrop contrast tradeoff, legacy overlay and opaque fallback. GPU appearance still needs desktop verification.");
        return 0;
    } catch (const winrt::hresult_error& error) { std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(error.code())); }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); }
    return 1;
}

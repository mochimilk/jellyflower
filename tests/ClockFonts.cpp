// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/clock_font.hpp"
#include "../src/clock_options.hpp"
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <cmath>
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Save(const acrylic::ClockPixels& image, const std::wstring& file) {
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (const auto value : image.pixels) {
        const char rgb[]{static_cast<char>(value >> 16), static_cast<char>(value >> 8), static_cast<char>(value)};
        out.write(rgb, 3);
    }
    std::ofstream metadata(file + L".json");
    metadata << "{\"baseline\":" << image.baseline << ",\"fontSize\":" << image.fontSize << "}";
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 4, "supply Playground file, Pangaia file, and output directory");
        acrylic::ClockPixels samples[2];
        wchar_t windows[MAX_PATH]{}; Check(GetWindowsDirectoryW(windows, MAX_PATH) != 0, "Windows font directory");
        acrylic::ClockFontRenderer segoe(std::wstring(windows) + L"\\Fonts\\segoeui.ttf");
        for (double scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            const auto reference = segoe.Render(L"Mon Jun 22  9:41 am", 12, scale, 0xFFF3F3F3);
            Save(reference, std::wstring(argv[3]) + L"/clock-font-segoe-" + std::to_wstring(static_cast<int>(scale * 100)) + L".ppm");
        }
        for (int i = 0; i < 2; ++i) {
            acrylic::ClockFontRenderer renderer(argv[i + 1]);
            const auto family = renderer.Family();
            Check(family.find(i == 0 ? L"Playground" : L"Pangaia") != std::wstring::npos, "actual file family loaded");
            for (double scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
                auto image = renderer.Render(L"Mon Jun 22  9:41 am", 12, scale, 0xFFF3F3F3);
                Check(image.width && image.height && image.pixels.size() == static_cast<size_t>(image.width) * image.height, "scaled pixel geometry");
                Check(std::abs(image.dipWidth * scale - image.width) < 0.0001
                    && std::abs(image.dipHeight * scale - image.height) < 0.0001,
                    "destination spans the raster exactly without a second resampling at fractional DPI");
                const auto ink = std::count_if(image.pixels.begin(), image.pixels.end(), [](uint32_t value) { return (value >> 24) > 32; });
                Check(ink > 40, "font actually rasterized");
                Check(image.fontSize > (i == 0 ? 17 : 12.5) && image.fontSize < (i == 0 ? 19 : 13.5), "visible-size correction for actual supplied typeface");
                const auto zero = renderer.Render(L"0", 12, scale, 0xFFFFFFFF);
                int lastInkRow = -1;
                for (UINT row = 0; row < zero.height; ++row) for (UINT col = 0; col < zero.width; ++col)
                    if ((zero.pixels[static_cast<size_t>(row) * zero.width + col] >> 24) > 48) lastInkRow = static_cast<int>(row);
                Check(lastInkRow >= 0 && std::abs((lastInkRow + 1) / scale - zero.baseline) < 1.25, "reported bitmap baseline agrees with actual clock digit ink");
                const auto swashes = renderer.Render(L"Thu May 28  11:59 pm", 12, scale, 0xFFFFFFFF);
                for (UINT row = 0; row < swashes.height; ++row) for (UINT col = 0; col < swashes.width; ++col)
                    if (row == 0 || col == 0 || row == swashes.height - 1 || col == swashes.width - 1)
                        Check((swashes.pixels[static_cast<size_t>(row) * swashes.width + col] >> 24) < 16, "decorative strokes and descenders retain unclipped bitmap padding");
                Check(std::all_of(image.pixels.begin(), image.pixels.end(), [](uint32_t value) {
                    return (value & 255) <= (value >> 24) && ((value >> 8) & 255) <= (value >> 24) && ((value >> 16) & 255) <= (value >> 24);
                }), "premultiplied alpha");
                const auto shortLine = renderer.Render(L"9:41 pm", 12, scale, 0xFF1A1A1A);
                Check(shortLine.dipWidth < image.dipWidth, "time-only mode shrinks naturally");
                const auto large = renderer.Render(L"Mon Jun 22  9:41 am", 18, scale, 0xFFF3F3F3);
                Check(large.width > image.width && large.height > image.height, "Windows text scaling geometry");
                Save(image, std::wstring(argv[3]) + L"/clock-font-" + std::to_wstring(i) + L"-" + std::to_wstring(static_cast<int>(scale * 100)) + L".ppm");
                if (scale == 1) samples[i] = std::move(image);
            }
            std::wprintf(L"Loaded %s\n", family.c_str());
            std::printf("Matched em size %.2f DIP for a 12-DIP Segoe clock; bitmap baseline %.2f DIP.\n", samples[i].fontSize, samples[i].baseline);
        }
        Check(samples[0].pixels != samples[1].pixels, "different fonts produce different actual glyphs");
        bool rejected = false;
        try { acrylic::ClockFontRenderer invalid(L"C:\\missing-clock-font.otf"); } catch (HRESULT) { rejected = true; }
        Check(rejected, "missing font rejected");
        Check(acrylic::DecodeClockOptions(0xFF00).font == acrylic::ClockFont::System, "invalid font setting defaults safely");
        Check(acrylic::DecodeClockOptions(0x0201).Packed() == 0x0201, "date/font atomic snapshot round trip");
        std::puts("PASS: actual external font families, exact pixel spans at 100/125/150/175/200%, text scaling, time-only width, premultiplied alpha, missing-file rejection, settings decoding.");
        return 0;
    } catch (HRESULT hr) { std::printf("FAIL HRESULT %08lx\n", static_cast<unsigned long>(hr)); return 1; }
    catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
}

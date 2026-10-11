// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace acrylic {
struct ClockPixels {
    UINT width = 0, height = 0;
    double dipWidth = 0, dipHeight = 0;
    double baseline = 0, fontSize = 0; // Bitmap baseline and optically matched em size, in DIPs.
    std::vector<uint32_t> pixels; // Premultiplied BGRA, top-down.
};
// A private DirectWrite collection reads the exact supplied file; no font
// installation, registry registration, or process-wide font replacement.
class ClockFontRenderer {
public:
    explicit ClockFontRenderer(const std::wstring& path);
    ~ClockFontRenderer();
    ClockFontRenderer(const ClockFontRenderer&) = delete;
    ClockFontRenderer& operator=(const ClockFontRenderer&) = delete;
    ClockPixels Render(const std::wstring& text, double size, double scale, uint32_t argb,
        const std::wstring& referenceFamily = L"Segoe UI", UINT referenceWeight = 400);
    std::wstring Family() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

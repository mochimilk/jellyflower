// SPDX-License-Identifier: GPL-3.0-or-later
#include "cursor_icon.hpp"
#include "cursor_shape.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace acrylic {
namespace {
struct CursorImage {
    HCURSOR cursor = nullptr;
    ICONINFO info{};
    bool owned = false;
    ~CursorImage() {
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        if (owned && cursor) DestroyCursor(cursor);
    }
};
bool ReadBitmap(HBITMAP bitmap, int width, int height, std::vector<uint32_t>& data) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    data.resize(static_cast<size_t>(width) * height);
    const HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    const bool read = GetDIBits(dc, bitmap, 0, height, data.data(), &info, DIB_RGB_COLORS) == height;
    DeleteDC(dc);
    return read;
}
struct Palette { COLORREF fill = RGB(255,255,255), stroke = RGB(0,0,0); };
bool ReadPalette(const wchar_t* file, Palette& palette) {
    CursorImage source;
    // The large frame has solid body/border swatches; small frames blend them.
    source.cursor = file && *file ? static_cast<HCURSOR>(LoadImageW(nullptr, file, IMAGE_CURSOR, 128, 128, LR_LOADFROMFILE)) : nullptr;
    source.owned = source.cursor != nullptr;
    if (!source.cursor) source.cursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!source.cursor || !GetIconInfo(source.cursor, &source.info)) return false;
    BITMAP bitmap{};
    if (!GetObjectW(source.info.hbmColor ? source.info.hbmColor : source.info.hbmMask, sizeof(bitmap), &bitmap)) return false;
    const int width = bitmap.bmWidth, height = source.info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return false;
    std::vector<uint32_t> color, mask, swatches;
    const bool hasColor = source.info.hbmColor && ReadBitmap(source.info.hbmColor, width, height, color);
    const bool hasAlpha = hasColor && std::any_of(color.begin(), color.end(), [](uint32_t pixel) { return (pixel >> 24) != 0; });
    if (!hasAlpha && !ReadBitmap(source.info.hbmMask, width, hasColor ? height : height * 2, mask)) return false;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t at = static_cast<size_t>(y) * width + x;
        const bool opaque = hasAlpha ? (color[at] >> 24) == 255 : (mask[at] & 0xffffff) == 0;
        if (opaque) swatches.push_back((hasColor ? color[at] : mask[at + static_cast<size_t>(width) * height]) & 0xffffff);
    }
    if (swatches.empty()) return false;
    // The two dominant opaque colors are the body and border. Ignore blended
    // edge/shadow samples; preserve tinted borders instead of menu-theme ink.
    std::sort(swatches.begin(), swatches.end());
    size_t firstCount = 0, secondCount = 0;
    uint32_t firstColor = 0, secondColor = 0;
    for (size_t first = 0; first < swatches.size();) {
        size_t last = first + 1;
        while (last < swatches.size() && swatches[last] == swatches[first]) ++last;
        const size_t count = last - first;
        if (count > firstCount) {
            secondCount = firstCount; secondColor = firstColor;
            firstCount = count; firstColor = swatches[first];
        } else if (count > secondCount) { secondCount = count; secondColor = swatches[first]; }
        first = last;
    }
    auto rgb = [](uint32_t pixel) { return RGB((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255); };
    palette.fill = rgb(firstColor);
    palette.stroke = rgb(secondCount ? secondColor : firstColor);
    return true;
}
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* bits = nullptr;
    explicit Surface(int size) {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap) { previous = SelectObject(dc, bitmap); bits = static_cast<uint32_t*>(pixels); }
    }
    ~Surface() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
bool PaintSvg(HDC dc, int size, COLORREF fill, COLORREF stroke) {
    const double scale = size / 24.0;
    std::array<POINT, cursor_shape::Points.size()> points{};
    for (size_t i = 0; i < points.size(); ++i) {
        points[i] = {static_cast<LONG>(std::lround((cursor_shape::Points[i].x - cursor_shape::CenterX) * scale + size / 2.0)),
            static_cast<LONG>(std::lround((cursor_shape::Points[i].y - cursor_shape::CenterY) * scale + size / 2.0))};
    }
    const LOGBRUSH penBrush{BS_SOLID, stroke, 0};
    const HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER,
        static_cast<DWORD>(std::max(1l, std::lround(cursor_shape::StrokeWidth * scale))), &penBrush, 0, nullptr);
    const HBRUSH brush = CreateSolidBrush(fill);
    if (!pen || !brush) { if (pen) DeleteObject(pen); if (brush) DeleteObject(brush); return false; }
    const auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
    SetMiterLimit(dc, 4, nullptr); // SVG's default miter limit.
    const bool painted = BeginPath(dc) && PolyDraw(dc, points.data(), cursor_shape::Types.data(), static_cast<int>(points.size()))
        && EndPath(dc) && StrokeAndFillPath(dc);
    if (!painted) AbortPath(dc);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
    DeleteObject(pen); DeleteObject(brush);
    return painted;
}
} // namespace

HICON CreateCursorMenuIcon(const wchar_t* file, int pixels) {
    if (pixels < 8 || pixels > 128) return nullptr;
    Palette palette;
    if (!ReadPalette(file, palette)) return nullptr;
    constexpr int Samples = 8, Area = Samples * Samples;
    const int size = pixels * Samples;
    Surface surface(size);
    if (!surface.bits) return nullptr;
    const size_t count = static_cast<size_t>(size) * size;
    std::fill_n(surface.bits, count, 0);
    if (!PaintSvg(surface.dc, size, RGB(255,255,255), RGB(255,255,255))) return nullptr;
    GdiFlush();
    std::vector<BYTE> coverage(count);
    for (size_t i = 0; i < count; ++i) coverage[i] = static_cast<BYTE>(surface.bits[i] & 255);
    std::fill_n(surface.bits, count, 0);
    if (!PaintSvg(surface.dc, size, palette.fill, palette.stroke)) return nullptr;
    GdiFlush();
    Surface output(pixels);
    if (!output.bits) return nullptr;
    for (int y = 0; y < pixels; ++y) for (int x = 0; x < pixels; ++x) {
        uint32_t a = 0, r = 0, g = 0, b = 0;
        for (int sy = 0; sy < Samples; ++sy) for (int sx = 0; sx < Samples; ++sx) {
            const size_t at = static_cast<size_t>(y * Samples + sy) * size + x * Samples + sx;
            const uint32_t color = surface.bits[at];
            a += coverage[at]; r += (color >> 16) & 255; g += (color >> 8) & 255; b += color & 255;
        }
        auto mean = [](uint32_t value) { return (value + Area / 2) / Area; };
        output.bits[static_cast<size_t>(y) * pixels + x] = (mean(a) << 24) | (mean(r) << 16) | (mean(g) << 8) | mean(b);
    }
    std::vector<BYTE> maskBits(static_cast<size_t>((pixels + 15) / 16 * 2) * pixels);
    const HBITMAP transparency = CreateBitmap(pixels, pixels, 1, 1, maskBits.data());
    ICONINFO icon{TRUE, 0, 0, transparency, output.bitmap};
    const HICON result = transparency ? CreateIconIndirect(&icon) : nullptr;
    if (transparency) DeleteObject(transparency);
    return result;
}
} // namespace acrylic

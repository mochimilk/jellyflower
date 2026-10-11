// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>

namespace acrylic {
// GDI does not preserve alpha for text and lines. Render the same foreground
// over black and white to recover its coverage, then copy premultiplied pixels
// to the glass frame. Empty pixels reveal DWM's acrylic, without fading text.
class MenuSurface {
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
public:
    DWORD* pixels = nullptr;
    MenuSurface(HDC target, const RECT& bounds, bool white) {
        const LONG width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) return;
        dc_ = CreateCompatibleDC(target);
        if (!dc_) return;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap_) return;
        previous_ = SelectObject(dc_, bitmap_);
        SetViewportOrgEx(dc_, -bounds.left, -bounds.top, nullptr);
        FillRect(dc_, &bounds, static_cast<HBRUSH>(GetStockObject(white ? WHITE_BRUSH : BLACK_BRUSH)));
    }
    ~MenuSurface() {
        if (previous_) SelectObject(dc_, previous_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
    }
    MenuSurface(const MenuSurface&) = delete;
    MenuSurface& operator=(const MenuSurface&) = delete;
    HDC Dc() const { return dc_; }
    template<class Draw> static bool Paint(HDC target, const RECT& bounds, Draw draw) {
        MenuSurface black(target, bounds, false), white(target, bounds, true);
        if (!black.pixels || !white.pixels) return false;
        draw(black.Dc()); draw(white.Dc());
        GdiFlush();
        const size_t count = static_cast<size_t>(bounds.right - bounds.left) * (bounds.bottom - bounds.top);
        for (size_t i = 0; i < count; ++i) {
            const DWORD b = black.pixels[i], w = white.pixels[i];
            int transparent = 0;
            for (int shift : {0, 8, 16})
                transparent = std::max(transparent, static_cast<int>((w >> shift) & 255) - static_cast<int>((b >> shift) & 255));
            const DWORD alpha = static_cast<DWORD>(255 - transparent);
            DWORD pixel = alpha << 24;
            for (int shift : {0, 8, 16}) pixel |= std::min(alpha, (b >> shift) & 255) << shift;
            black.pixels[i] = pixel;
        }
        return BitBlt(target, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            black.Dc(), bounds.left, bounds.top, SRCCOPY) != FALSE;
    }
};
}

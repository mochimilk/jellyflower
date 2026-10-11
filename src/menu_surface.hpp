// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

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
        // GDI text smoothing consults the DC background color even with a
        // transparent text background. Match each coverage pass to its pixels.
        SetBkColor(dc_, white ? RGB(255,255,255) : RGB(0,0,0));
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
    static void Text(HDC target, RECT bounds, const std::wstring& text, HFONT font, COLORREF color, UINT flags) {
        // Render one neutral coverage mask for both themes. GDI's gamma varies
        // with foreground/background colors; drawing twice with different
        // colors made white text heavier and ClearType text look hollow.
        MenuSurface mask(target, bounds, true);
        if (!mask.pixels) { DrawTextW(target, text.c_str(), -1, &bounds, flags); return; }
        const auto previous = SelectObject(mask.Dc(), font);
        SetBkMode(mask.Dc(), TRANSPARENT); SetTextColor(mask.Dc(), RGB(0,0,0));
        RECT layout = bounds; DrawTextW(mask.Dc(), text.c_str(), -1, &layout, flags); GdiFlush();
        const size_t count = static_cast<size_t>(bounds.right-bounds.left)*(bounds.bottom-bounds.top);
        for (size_t i=0; i<count; ++i) {
            const DWORD alpha = 255-(mask.pixels[i]&255);
            mask.pixels[i]=(alpha<<24)|((GetRValue(color)*alpha/255)<<16)|((GetGValue(color)*alpha/255)<<8)|(GetBValue(color)*alpha/255);
        }
        const BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        AlphaBlend(target,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
            mask.Dc(),bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,blend);
        GdiFlush(); // Finish reading the DIB before its storage is released.
        SelectObject(mask.Dc(),previous);
    }
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
        const bool copied = BitBlt(target, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            black.Dc(), bounds.left, bounds.top, SRCCOPY) != FALSE;
        GdiFlush();
        return copied;
    }
};
}

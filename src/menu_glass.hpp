// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>

namespace acrylic {
// Fluted glass behind a menu: what's behind the window, seen through vertical reeds and tinted like the
// menu's acrylic. Windows.UI.Composition draws above a window's own GDI content, so the menu's composed
// pixels are copied into a surface on top of the glass.
class MenuGlass {
public:
    MenuGlass();
    ~MenuGlass();
    MenuGlass(const MenuGlass&) = delete;
    MenuGlass& operator=(const MenuGlass&) = delete;
    // Puts the glass behind the window, or retints it when it's already there. False leaves the window
    // as it was.
    bool Attach(HWND window, UINT dpi, bool light);
    // Copies premultiplied pixels for a rectangle of the client area above the glass.
    bool Present(const RECT& bounds, const DWORD* pixels);
    void Detach();
    bool Attached() const { return impl_ != nullptr; }
    // The last failure to draw a menu's glass, for --diagnose; S_OK once one is drawn again.
    static HRESULT LastResult();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>

namespace acrylic {
// Posted to the owner when a button in the top edge is clicked, with the
// BarButton (top_bar.hpp) in wParam. For the menu button the owner reads it
// with MenuButton() and opens the jellyflower menu below it.
inline constexpr UINT BarButtonMessage = WM_APP + 61;

struct ScreenOptions {
    bool statusBar = true; // The time in a compact display's top margin.
    bool topBar = false;   // The top bar on every display (top_bar.hpp).
    bool date = true;      // The top bar's clock shows the date.
    bool light = false;    // A light desktop: dark ink.
};

// Reserves strips along display edges as app bars and draws in the top one.
// Every compact display (compact.hpp) gets all four edges, and its top margin
// shows the status bar: the time with a small chevron that opens the
// jellyflower menu, so the taskbar's notification area can stay empty. With
// the top bar on, every other display gets the top edge too, and every top
// edge draws the top bar instead. It runs in the controller only: nothing is
// loaded into the shell or into other apps. Windows drops an app bar together
// with its window, so even a killed controller gives the screen its full work
// area back.
class CompactScreen {
public:
    CompactScreen();
    ~CompactScreen();
    CompactScreen(const CompactScreen&) = delete;
    CompactScreen& operator=(const CompactScreen&) = delete;
    // The window that receives BarButtonMessage.
    void Owner(HWND owner) noexcept;
    // Frames the displays the options ask for and releases the others. App
    // bars are only renegotiated when a display changes, so the 5 s refresh
    // stays cheap.
    void Refresh(const ScreenOptions& options);
    // Explorer restarted and forgot every app bar.
    void ShellRestarted() noexcept;
    // A window covering a whole display hides the top edge's drawing there.
    void ForegroundChanged();
    void Remove() noexcept;
    // Compact displays with their margins in place.
    LONG Displays() const noexcept;
    // Displays showing the top bar.
    LONG TopBars() const noexcept;
    HRESULT Result() const noexcept;
    // The last clicked menu button, in screen pixels, and its display's DPI.
    // False once that display has stopped showing it.
    bool MenuButton(RECT& bounds, UINT& dpi) const;
    // Keeps the clicked menu button highlighted while its menu is open.
    void MenuOpen(bool open);
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace acrylic

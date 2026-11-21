// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>

namespace acrylic {
// Posted to the owner when the status bar's menu button is clicked. The owner
// reads the button with MenuButton() and opens the jellyflower menu below it.
inline constexpr UINT StatusBarMenuMessage = WM_APP + 61;

// Frames every compact display (compact.hpp) with an app bar on each edge and
// draws the status bar in the top one: the time with a small chevron that
// opens the jellyflower menu, so the taskbar's notification area can stay
// empty. It runs in the controller only: nothing is loaded into the shell or
// into other apps. Windows drops an app bar together with its window, so even
// a killed controller gives the screen its full work area back.
class CompactScreen {
public:
    CompactScreen();
    ~CompactScreen();
    CompactScreen(const CompactScreen&) = delete;
    CompactScreen& operator=(const CompactScreen&) = delete;
    // The window that receives StatusBarMenuMessage.
    void Owner(HWND owner) noexcept;
    // Frames the compact displays and releases the others. App bars are only
    // renegotiated when a display changes, so the 5 s refresh stays cheap.
    void Refresh(bool statusBar, bool light);
    // Explorer restarted and forgot every app bar.
    void ShellRestarted() noexcept;
    // A window covering a whole display hides the status bar there.
    void ForegroundChanged();
    void Remove() noexcept;
    LONG Displays() const noexcept;
    HRESULT Result() const noexcept;
    // The last clicked menu button, in screen pixels, and its display's DPI.
    // False once that display has stopped being compact.
    bool MenuButton(RECT& bounds, UINT& dpi) const;
    // Keeps the clicked button highlighted while its menu is open.
    void MenuOpen(bool open);
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace acrylic

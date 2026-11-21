// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.h>
#include <memory>
#include <string_view>

namespace acrylic {
using TrayClassName = winrt::hstring(*)(const winrt::Windows::UI::Xaml::DependencyObject&);
// The taskbar notification area that contains this element. The overflow
// flyout is a separate tree and is never returned.
winrt::Windows::UI::Xaml::FrameworkElement FindTrayFrame(
    const winrt::Windows::UI::Xaml::DependencyObject& object, TrayClassName className = nullptr);
// jellyflower's notification icon exposes its tooltip as its accessible name,
// in whichever display language the controller last used.
bool IsJellyflowerTip(std::wstring_view name) noexcept;

// Collapses the known notification-area parts except jellyflower's own icon:
// other notification icons, the input indicator and status stacks, Quick
// Settings and the clock/notification button. The show-hidden-icons chevron
// is collapsed only while jellyflower's icon is on this taskbar, so its menu
// stays reachable. Each part's own Visibility value or binding is restored.
class TrayOnlyView {
public:
    explicit TrayOnlyView(const winrt::Windows::UI::Xaml::FrameworkElement& frame, TrayClassName className = nullptr);
    ~TrayOnlyView();
    TrayOnlyView(const TrayOnlyView&) = delete;
    TrayOnlyView& operator=(const TrayOnlyView&) = delete;
    // Throws only when the tray cannot be read; per-part failures are kept in Error().
    void Refresh(bool enabled);
    void Restore() noexcept;
    unsigned Hidden() const noexcept;
    bool Found() const noexcept;
    HRESULT Error() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

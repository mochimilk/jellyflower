// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include "dock_geometry.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace acrylic {
using DockClassName = winrt::hstring(*)(const winrt::Windows::UI::Xaml::DependencyObject&);

// Reshapes one taskbar's background into a floating pill behind its app
// group. BackgroundFill keeps its brush; BackgroundStroke becomes the pill's
// outline, drawn with the stroke brush the caller supplies (normally the
// stock stroke fill). Fills are left to the acrylic adapter. Every changed
// property's own value or binding is restored.
class DockView {
public:
    explicit DockView(const winrt::Windows::UI::Xaml::FrameworkElement& frame, DockClassName className = nullptr);
    ~DockView();
    DockView(const DockView&) = delete;
    DockView& operator=(const DockView&) = delete;
    // Never throws: a failure restores the stock bar and is kept in Error().
    // Without a visible app group the bar stays stock until one appears.
    // height: the taller bar's DIPs (bar_height.hpp), or 0 for the stock bar.
    // The frame and the notification area are held at that height, and each
    // button's hover and running highlight stays a centered square in it.
    // While the dock is on, those highlights have app-icon corners.
    void Refresh(bool enabled, const winrt::Windows::UI::Xaml::Media::Brush& stroke, LONG height = 0);
    void Restore() noexcept;
    bool Applied() const noexcept;
    // The taller bar's DIPs while the frames are held at it, else 0.
    LONG Height() const noexcept;
    // XAML-root DIPs: the pill first, then every other part that takes input
    // (parts of the bar outside the pill and the notification area).
    const std::vector<DockBox>& Hits() const noexcept;
    // Runs on the UI thread after a layout pass changed the hits.
    void OnMoved(std::function<void()> callback);
    HRESULT Error() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// The taskbar window on this thread whose XAML island has this pixel size.
// Identical taskbars on identical monitors are told apart by a screen point
// inside the island, when the caller has one. Null (with error set) when
// none or more than one match.
HWND FindDockWindow(SIZE island, const POINT* screen, DWORD thread, POINT& offset, HRESULT& error) noexcept;

// Owns a window region. The stock taskbar has either no region or one
// covering the whole window (Windows 11 sets the latter); that stock state is
// kept and put back on restore, and when the shell writes it again while the
// dock is on it becomes the new stock state. Any other region belongs to
// another program and is never replaced or removed.
class DockRegion {
public:
    DockRegion() = default;
    ~DockRegion() { Restore(); }
    DockRegion(const DockRegion&) = delete;
    DockRegion& operator=(const DockRegion&) = delete;
    HRESULT Apply(HWND window, const std::vector<RECT>& rects) noexcept;
    void Restore() noexcept;
    bool Applied() const noexcept { return window_ != nullptr; }
private:
    enum class Stock { None, Whole, Other };
    static Stock Current(HWND window) noexcept;
    bool Ours() const noexcept;
    HWND window_ = nullptr;
    std::vector<RECT> rects_;
    bool whole_ = false; // The stock region covered the whole window.
};
}

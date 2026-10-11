// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <memory>
namespace acrylic {
using ChevronClassName = winrt::hstring(*)(const winrt::Windows::UI::Xaml::DependencyObject&);
winrt::Windows::UI::Xaml::FrameworkElement FindChevronRoot(
    const winrt::Windows::UI::Xaml::DependencyObject& object, ChevronClassName className = nullptr);
winrt::Windows::UI::Xaml::Controls::TextBlock FindChevronGlyph(
    const winrt::Windows::UI::Xaml::DependencyObject& object, ChevronClassName className = nullptr);
class ChevronView {
public:
    explicit ChevronView(const winrt::Windows::UI::Xaml::Controls::TextBlock& glyph);
    ~ChevronView();
    ChevronView(const ChevronView&) = delete;
    ChevronView& operator=(const ChevronView&) = delete;
    void Refresh(bool enabled);
    void Restore();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

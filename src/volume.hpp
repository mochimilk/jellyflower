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
using XamlClassName = winrt::hstring(*)(const winrt::Windows::UI::Xaml::DependencyObject&);
winrt::Windows::UI::Xaml::FrameworkElement FindVolumeRoot(
    const winrt::Windows::UI::Xaml::DependencyObject& object, XamlClassName className = nullptr);
// Accept only the documented speaker slot under ControlCenterButton and a
// recognized speaker glyph. Unknown shell layouts remain untouched.
winrt::Windows::UI::Xaml::Controls::Grid FindVolumeGrid(
    const winrt::Windows::UI::Xaml::DependencyObject& object, XamlClassName className = nullptr);
class VolumeView {
public:
    explicit VolumeView(const winrt::Windows::UI::Xaml::Controls::Grid& grid);
    ~VolumeView();
    VolumeView(const VolumeView&) = delete;
    VolumeView& operator=(const VolumeView&) = delete;
    void Refresh(bool enabled);
    void Restore();
    bool Applied() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

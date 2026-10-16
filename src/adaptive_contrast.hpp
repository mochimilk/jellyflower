// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.h>
#include <memory>
#include <cstdint>

namespace acrylic {
using AdaptiveClassName=winrt::hstring(*)(winrt::Windows::UI::Xaml::DependencyObject const&);
bool IsAdaptiveTrayTarget(winrt::Windows::UI::Xaml::DependencyObject const&,AdaptiveClassName=nullptr);
struct AdaptiveStats {uint64_t bitmapReads=0,maskBuilds=0;};
// Keeps native input, layout, artwork, bindings and colored status states.
// Bitmap inspection reads Explorer's tiny CPU-resident WriteableBitmap; it
// never captures the screen or reads the desktop back from the GPU.
class AdaptiveView {
    struct Impl;
    std::shared_ptr<Impl> impl;
public:
    explicit AdaptiveView(winrt::Windows::UI::Xaml::DependencyObject const&,bool(*ownerAlive)()=nullptr);
    ~AdaptiveView();
    AdaptiveView(AdaptiveView const&)=delete;
    AdaptiveView& operator=(AdaptiveView const&)=delete;
    void Refresh(bool enabled,bool visible,uint64_t now=GetTickCount64());
    void Restore();
    bool Applied() const;
    bool IsBitmap() const;
    HRESULT Error() const;
    AdaptiveStats Stats() const;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <cmath>
#include <algorithm>
namespace acrylic {
// Start's menu column has a fixed native width (642 DIP for the small layout)
// inside a padded, centered frame. A display narrower than both, such as a
// portrait tablet at 300%, cuts the menu's right edge off. Narrow layout
// gives the menu the display's full width instead, with four pinned columns.
// The grids read this per-UI-thread state to lay out to match; Start has one
// menu per UI thread.
struct StartNarrowLayout {
    bool active=false;
    double removed=0; // native menu width minus the applied width
};
inline StartNarrowLayout& StartNarrow() noexcept {thread_local StartNarrowLayout layout;return layout;}
// The native width whenever it fits; otherwise all the width there is.
inline double StartMenuWidth(double native,double available) noexcept {
    if(!std::isfinite(native) || native<=0 || !std::isfinite(available) || available<=0 || native<=available)return native;
    return available;
}
inline bool IsStartMenuColumn(const sx::FrameworkElement& element,StartClassName name=nullptr) {
    if(!element || element.Name()!=L"MainMenu" || !element.try_as<sx::Controls::Grid>())return false;
    const auto animation=sx::Media::VisualTreeHelper::GetParent(element).try_as<sx::Controls::Grid>();
    if(!animation || animation.Name()!=L"AnimationRoot")return false;
    const auto frame=sx::Media::VisualTreeHelper::GetParent(animation).try_as<sx::Controls::Grid>();
    if(!frame || frame.Name()!=L"FrameRoot")return false;
    auto parent=sx::Media::VisualTreeHelper::GetParent(frame);
    for(unsigned depth=0;parent && depth<8;++depth) {
        const auto type=name?name(parent):winrt::get_class_name(parent);
        const std::wstring_view text(type);
        if(text.substr(0,10)==L"StartMenu." || text.substr(0,12)==L"StartDocked.")return true;
        parent=sx::Media::VisualTreeHelper::GetParent(parent);
    }
    return false;
}
// Owns MainMenu.Width. The shell sets it in code, so later native widths (the
// large layout, a display change) pass through Fit and become the new original.
class StartFrame {
    sx::FrameworkElement menu_;
    sx::Controls::Grid frame_{nullptr};
    sx::FrameworkElement host_{nullptr};
    StartSearchValues values_;
    winrt::event_token size_{};
    bool applied_=false;
    double Available() const {
        // The native frame keeps its padding, so the menu keeps Windows' gap
        // (and room for its shadow) at the display edges.
        const auto padding=frame_.Padding();const auto margin=frame_.Margin();
        return host_.ActualWidth()-padding.Left-padding.Right-margin.Left-margin.Right;
    }
    double Fit(double native) noexcept {
        double width=native;
        try{if(host_ && host_.ActualWidth()>0)width=StartMenuWidth(native,Available());}catch(...){}
        auto& narrow=StartNarrow();
        narrow.active=std::isfinite(width) && std::isfinite(native) && width<native-.01;
        narrow.removed=narrow.active?native-width:0;
        return width;
    }
    void Update() noexcept {
        if(!applied_)return;
        try {
            const double native=winrt::unbox_value<double>(values_.Original(menu_,sx::FrameworkElement::WidthProperty()));
            const double width=Fit(native);
            if(std::isfinite(width) && !(std::abs(menu_.Width()-width)<=.01))values_.Update(menu_,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));
        }catch(...){}
    }
public:
    explicit StartFrame(const sx::FrameworkElement& menu):menu_(menu){}
    ~StartFrame(){Restore();}
    StartFrame(const StartFrame&)=delete;StartFrame& operator=(const StartFrame&)=delete;
    void Refresh(bool enabled) {
        if(!enabled){Restore();return;}
        if(!applied_) {
            frame_=sx::Media::VisualTreeHelper::GetParent(sx::Media::VisualTreeHelper::GetParent(menu_)).try_as<sx::Controls::Grid>();
            host_=frame_?sx::Media::VisualTreeHelper::GetParent(frame_).try_as<sx::FrameworkElement>():nullptr;
            // An auto-sized menu already fits its frame; only a fixed width is adapted.
            if(!host_ || !std::isfinite(menu_.Width())){frame_=nullptr;host_=nullptr;return;}
            applied_=true;
            try {
                values_.Set(menu_,sx::FrameworkElement::WidthProperty(),nullptr,[this](const auto& native){return winrt::box_value(Fit(winrt::unbox_value<double>(native)));});
                // The Start window follows the display: rotation, scale and
                // monitor changes all arrive as a new frame host size.
                size_=host_.SizeChanged([this](auto&&,auto&&){Update();});
            }catch(...){Restore();throw;}
        }
        Update();
    }
    void Restore() noexcept {
        try{if(host_ && size_.value)host_.SizeChanged(size_);}catch(...){}
        size_={};values_.Clear();frame_=nullptr;host_=nullptr;
        if(applied_)StartNarrow()={};
        applied_=false;
    }
    bool Applied() const noexcept {return applied_;}
};
}

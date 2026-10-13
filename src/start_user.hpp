// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include "start_metrics.hpp"
#include <algorithm>
namespace acrylic {
inline bool IsStartUserButton(const sx::Controls::Control& button, StartClassName name=nullptr) {
    if(!button || button.Name()!=L"UserTileButton") return false;
    const auto type=name?name(button):winrt::get_class_name(button);
    if(type!=L"StartMenu.NavigationPaneButton" && type!=L"StartDocked.NavigationPaneButton") return false;
    auto parent=sx::Media::VisualTreeHelper::GetParent(button);
    for(unsigned depth=0;parent && depth<8;++depth) {
        const auto owner=name?name(parent):winrt::get_class_name(parent);
        if(owner==L"StartMenu.UserTileView" || owner==L"StartDocked.UserTileView") return true;
        parent=sx::Media::VisualTreeHelper::GetParent(parent);
    }
    return false;
}
class StartUser {
    sx::Controls::Control button_;
    sx::FrameworkElement template_{nullptr};
    StartSearchValues values_;
    bool applied_=false;
public:
    explicit StartUser(const sx::Controls::Control& button):button_(button) {}
    StartUser(const StartUser&)=delete;StartUser& operator=(const StartUser&)=delete;
    ~StartUser(){Restore();}
    void Refresh(bool enabled) {
        if(!enabled){Restore();return;}
        if(!sx::Media::VisualTreeHelper::GetChildrenCount(button_)){Restore();return;}
        const auto root=sx::Media::VisualTreeHelper::GetChild(button_,0).try_as<sx::FrameworkElement>();
        if(applied_ && root==template_)return;
        if(applied_)Restore();
        if(!root)return;
        const auto icon=FindStartElement(root,L"UserTileIcon").try_as<sx::Controls::Grid>();
        const auto background=FindStartElement(root,L"BackgroundBorder").try_as<sx::Controls::Border>();
        const auto label=FindStartElement(root,L"UserTileNameText").try_as<sx::Controls::TextBlock>();
        if(!icon || !background || !label)return;
        try {
            template_=root;
            values_.Set(icon,sx::FrameworkElement::WidthProperty(),winrt::box_value(StartMetrics::UserAvatar));
            values_.Set(icon,sx::FrameworkElement::HeightProperty(),winrt::box_value(StartMetrics::UserAvatar));
            values_.Set(icon,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));
            values_.Set(icon,sx::FrameworkElement::MinHeightProperty(),winrt::box_value(0.0));
            values_.Set(icon,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Center));
            values_.Set(icon,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
            // Native templates include the control border around the padding.
            // Keep six DIP total on every edge and the existing 40-DIP target.
            const auto border=button_.BorderThickness();
            const auto edge=[](double thickness){return std::max(0.0,StartMetrics::UserBadgePadding-thickness);};
            values_.Set(button_,sx::Controls::Control::PaddingProperty(),winrt::box_value(sx::Thickness{edge(border.Left),edge(border.Top),edge(border.Right),edge(border.Bottom)}));
            values_.Set(button_,sx::FrameworkElement::MinHeightProperty(),nullptr,[](const auto& original){return winrt::box_value(std::max(StartMetrics::UserBadgeHeight,winrt::unbox_value<double>(original)));});
            constexpr double radius=StartMetrics::UserBadgeRadius;
            values_.Set(background,sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(sx::CornerRadius{radius,radius,radius,radius}));
            applied_=true;
        }catch(...){Restore();throw;}
    }
    void Restore() noexcept {applied_=false;values_.Clear();template_=nullptr;}
    bool Applied()const noexcept{return applied_;}
};
}

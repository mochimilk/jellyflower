// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include "start_metrics.hpp"
#include <algorithm>
#include <limits>
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
            // Preserve the native horizontal anchor and use the same padding
            // vertically. Auto-sized native footer hosts make room for it.
            values_.Set(button_,sx::Controls::Control::PaddingProperty(),nullptr,[](const auto& original){
                auto p=winrt::unbox_value<sx::Thickness>(original);p.Top=p.Left;p.Bottom=p.Right;return winrt::box_value(p);
            });
            values_.Set(button_,sx::FrameworkElement::MinHeightProperty(),nullptr,[](const auto& original){return winrt::box_value(std::max(StartMetrics::UserBadgeHeight,winrt::unbox_value<double>(original)));});
            for(const auto& part:{button_.as<sx::FrameworkElement>(),icon.as<sx::FrameworkElement>(),label.as<sx::FrameworkElement>()})
                values_.Set(part,sx::Controls::ToolTipService::ToolTipProperty(),nullptr);
            auto parent=sx::Media::VisualTreeHelper::GetParent(button_);
            for(unsigned depth=0;parent && depth<12;++depth) {
                if(auto host=parent.try_as<sx::FrameworkElement>();host && (host.Name()==L"UserTile" || host.Name()==L"NavPanePlaceholder")) {
                    values_.Set(host,sx::FrameworkElement::HeightProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
                    if(host.Name()==L"UserTile")values_.Set(host,sx::Controls::ToolTipService::ToolTipProperty(),nullptr);
                }
                parent=sx::Media::VisualTreeHelper::GetParent(parent);
            }
            constexpr double radius=StartMetrics::UserBadgeRadius;
            values_.Set(background,sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(sx::CornerRadius{radius,radius,radius,radius}));
            applied_=true;
        }catch(...){Restore();throw;}
    }
    void Restore() noexcept {applied_=false;values_.Clear();template_=nullptr;}
    bool Applied()const noexcept{return applied_;}
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
namespace acrylic {
enum class StartCornerRule {None,Shadow,Content};
inline StartCornerRule StartCornerFor(const sx::FrameworkElement& element,StartClassName name=nullptr) {
    if(!element)return StartCornerRule::None;
    const auto id=element.Name();auto parent=sx::Media::VisualTreeHelper::GetParent(element).try_as<sx::FrameworkElement>();
    const bool shadow=element.try_as<sx::Controls::Border>() && (id==L"StartDropShadow" || id==L"RightCompanionDropShadow"
        || (id==L"dropshadow" && parent && parent.Name()==L"BorderGrid"));
    const bool content=element.try_as<sx::Controls::Grid>() && id==L"MainContent" && parent
        && (parent.Name()==L"MainMenu" || parent.Name()==L"CompanionRoot");
    if(!shadow && !content)return StartCornerRule::None;
    auto current=sx::Media::VisualTreeHelper::GetParent(element);
    for(unsigned i=0;current && i<48;++i) {
        const auto type=name?name(current):winrt::get_class_name(current);const std::wstring_view text(type);
        if(text.substr(0,10)==L"StartMenu." || text.substr(0,12)==L"StartDocked.")return shadow?StartCornerRule::Shadow:StartCornerRule::Content;
        current=sx::Media::VisualTreeHelper::GetParent(current);
    }
    return StartCornerRule::None;
}
// Let XAML own its existing layout/compositor clips. Replacing a handout
// visual's Clip conflicts with the native Start animation/layout pipeline.
class StartCorners {
    sx::FrameworkElement element_;
    StartCornerRule rule_;
    StartSearchValues values_;
    bool applied_=false;
public:
    StartCorners(const sx::FrameworkElement& element,StartCornerRule rule):element_(element),rule_(rule) {}
    ~StartCorners(){Restore();}
    StartCorners(const StartCorners&)=delete;StartCorners& operator=(const StartCorners&)=delete;
    void Refresh(bool enabled) {
        if(!enabled){Restore();return;}
        if(applied_ || rule_==StartCornerRule::None)return;
        try {
            const double radius=rule_==StartCornerRule::Shadow?20:19;
            const auto key=rule_==StartCornerRule::Shadow?sx::Controls::Border::CornerRadiusProperty():sx::Controls::Grid::CornerRadiusProperty();
            values_.Set(element_,key,winrt::box_value(sx::CornerRadius{radius,radius,radius,radius}));
            applied_=true;
        }catch(...){Restore();throw;}
    }
    void Restore() noexcept {applied_=false;values_.Clear();}
    bool Applied()const noexcept{return applied_;}
};
}

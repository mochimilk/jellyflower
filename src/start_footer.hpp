// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_spacing.hpp"
namespace acrylic {
inline bool IsStartFooter(const sx::FrameworkElement& element,StartClassName name=nullptr) {
    return element && element.Name()==L"NavPanePlaceholder" && StartSpacingFor(element,name)==StartSpacingRule::Both;
}
enum class StartFooterKind { None, Host, Action };
inline StartFooterKind StartFooterPartFor(const sx::FrameworkElement& element,StartClassName name=nullptr) {
    if(!element)return StartFooterKind::None;
    const auto type=name?name(element):winrt::get_class_name(element);
    const std::wstring_view t(type);
    if(t.substr(0,10)!=L"StartMenu." && t.substr(0,12)!=L"StartDocked.")return StartFooterKind::None;
    const auto leaf=t.substr(t.find(L'.')+1);
    const bool action=leaf==L"AppListViewItem" || (leaf==L"NavigationPaneButton" && element.Name()==L"PowerButton");
    const bool host=leaf==L"NavigationPaneView" || leaf==L"UserTileView" || leaf==L"PowerOptionsView"
        || (leaf==L"AppListView" && element.Name()==L"NavigationPanePlacesListView");
    if(!action && !host)return StartFooterKind::None;
    auto parent=sx::Media::VisualTreeHelper::GetParent(element);
    for(unsigned depth=0;parent && depth<24;++depth){if(IsStartFooter(parent.try_as<sx::FrameworkElement>(),name))return action?StartFooterKind::Action:StartFooterKind::Host;parent=sx::Media::VisualTreeHelper::GetParent(parent);}
    return StartFooterKind::None;
}
class StartFooter {
    sx::FrameworkElement element_;
    sx::Controls::RowDefinition row_{nullptr};
    StartSearchValues values_;
    int64_t heightToken_=0,minToken_=0;
    bool applied_=false,writing_=false,ownMin_=false;
    void Floor() noexcept {
        if(!row_ || !ownMin_)return;
        try {
            const auto height=row_.Height();writing_=true;
            if(height.GridUnitType==sx::GridUnitType::Pixel)row_.MinHeight(height.Value+StartMetrics::FooterHeightAddition);
            else row_.ClearValue(sx::Controls::RowDefinition::MinHeightProperty());
            writing_=false;
        }catch(...){writing_=false;}
    }
    void ReleaseRow() noexcept {
        try{if(row_){if(heightToken_)row_.UnregisterPropertyChangedCallback(sx::Controls::RowDefinition::HeightProperty(),heightToken_);if(minToken_)row_.UnregisterPropertyChangedCallback(sx::Controls::RowDefinition::MinHeightProperty(),minToken_);if(ownMin_)row_.ClearValue(sx::Controls::RowDefinition::MinHeightProperty());}}catch(...){}
        row_=nullptr;heightToken_=minToken_=0;ownMin_=false;
    }
public:
    explicit StartFooter(const sx::FrameworkElement& element):element_(element){}
    ~StartFooter(){Restore();}
    void Refresh(bool enabled){
        if(!enabled){Restore();return;}
        if(!applied_){values_.Set(element_,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));applied_=true;}
        auto grid=sx::Media::VisualTreeHelper::GetParent(element_).try_as<sx::Controls::Grid>();
        const auto index=sx::Controls::Grid::GetRow(element_);
        auto row=grid && index>=0 && static_cast<unsigned>(index)<grid.RowDefinitions().Size()?grid.RowDefinitions().GetAt(index):sx::Controls::RowDefinition{nullptr};
        if(row==row_)return;
        ReleaseRow();row_=row;if(!row_)return;
        // Height may be bound by the shell. Leave it intact and add a minimum
        // only when the native minimum is unset. Never overwrite an unknown
        // explicit/bound constraint on a future Start layout.
        ownMin_=row_.ReadLocalValue(sx::Controls::RowDefinition::MinHeightProperty())==sx::DependencyProperty::UnsetValue();
        heightToken_=row_.RegisterPropertyChangedCallback(sx::Controls::RowDefinition::HeightProperty(),[this](auto&&,auto&&){if(!writing_)Floor();});
        minToken_=row_.RegisterPropertyChangedCallback(sx::Controls::RowDefinition::MinHeightProperty(),[this](auto&&,auto&&){if(writing_)return;ownMin_=row_.ReadLocalValue(sx::Controls::RowDefinition::MinHeightProperty())==sx::DependencyProperty::UnsetValue();Floor();});
        Floor();
    }
    void Restore()noexcept{ReleaseRow();values_.Clear();applied_=false;}
    bool Applied()const noexcept{return applied_;}
};
class StartFooterPart {
    sx::FrameworkElement element_;
    StartFooterKind kind_;
    sx::DependencyObject template_{nullptr};
    StartSearchValues values_,parts_;
    bool applied_=false,complete_=false;
public:
    StartFooterPart(const sx::FrameworkElement& element,StartFooterKind kind):element_(element),kind_(kind){}
    ~StartFooterPart(){Restore();}
    void Refresh(bool enabled){
        if(!enabled){Restore();return;}
        if(!applied_){values_.Set(element_,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
            if(kind_==StartFooterKind::Action)if(auto control=element_.try_as<sx::Controls::Control>())values_.Set(control,sx::Controls::Control::VerticalContentAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
            applied_=true;}
        if(kind_!=StartFooterKind::Action || !sx::Media::VisualTreeHelper::GetChildrenCount(element_))return;
        const auto root=sx::Media::VisualTreeHelper::GetChild(element_,0);
        if(root==template_ && complete_)return;
        parts_.Clear();template_=root;complete_=false;
        const sx::CornerRadius radius{StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius};
        for(const auto id:{L"BackgroundBorder",L"HighContrastBorder"})if(auto border=FindStartElement(root,id).try_as<sx::Controls::Border>()){
            parts_.Set(border,sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(radius));if(std::wstring_view(id)==L"BackgroundBorder")complete_=true;}
    }
    void Restore()noexcept{parts_.Clear();values_.Clear();template_=nullptr;complete_=applied_=false;}
    bool Applied()const noexcept{return applied_;}
};
}

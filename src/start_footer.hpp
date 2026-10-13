// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_spacing.hpp"
#include <cmath>
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
    sx::FrameworkElement powerHost_{nullptr};
    StartSearchValues values_,parts_;
    StartClassName name_;
    int64_t widthToken_=0;
    winrt::event_token sizeToken_{};
    bool applied_=false,complete_=false;
    double Side()const {const double width=element_.Width();return std::isfinite(width) && width>0?width:element_.ActualWidth();}
    void Square()noexcept{
        try{const double side=Side();if(side<=0 || !std::isfinite(side))return;
            for(const auto& target:{element_,powerHost_})if(target && target.Height()!=side)values_.Update(target,sx::FrameworkElement::HeightProperty(),winrt::box_value(side));
        }catch(...){}
    }
public:
    StartFooterPart(const sx::FrameworkElement& element,StartFooterKind kind,StartClassName name=nullptr):element_(element),kind_(kind),name_(name){}
    ~StartFooterPart(){Restore();}
    void Refresh(bool enabled){
        if(!enabled){Restore();return;}
        if(!applied_){values_.Set(element_,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
            if(kind_==StartFooterKind::Action)if(auto control=element_.try_as<sx::Controls::Control>())values_.Set(control,sx::Controls::Control::VerticalContentAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
            if(kind_==StartFooterKind::Action && element_.Name()==L"PowerButton"){
                auto parent=sx::Media::VisualTreeHelper::GetParent(element_);
                for(unsigned depth=0;parent && depth<8;++depth){
                    const auto type=name_?name_(parent):winrt::get_class_name(parent);
                    if(type==L"StartMenu.PowerOptionsView" || type==L"StartDocked.PowerOptionsView"){powerHost_=parent.try_as<sx::FrameworkElement>();break;}
                    parent=sx::Media::VisualTreeHelper::GetParent(parent);
                }
                // Native power is Width=40, Height=Auto. Center alignment must
                // not reduce its hit/hover target to the glyph's desired height.
                for(const auto& target:{element_,powerHost_})if(target)values_.Set(target,sx::FrameworkElement::HeightProperty(),nullptr,[this](const auto& original){const auto side=Side();return side>0 && std::isfinite(side)?winrt::box_value(side):original;});
                widthToken_=element_.RegisterPropertyChangedCallback(sx::FrameworkElement::WidthProperty(),[this](auto&&,auto&&){Square();});
                sizeToken_=element_.SizeChanged([this](auto&&,auto&&){Square();});
            }
            applied_=true;}
        if(kind_!=StartFooterKind::Action || !sx::Media::VisualTreeHelper::GetChildrenCount(element_))return;
        const auto root=sx::Media::VisualTreeHelper::GetChild(element_,0);
        if(root==template_ && complete_)return;
        parts_.Clear();template_=root;complete_=false;
        const sx::CornerRadius radius{StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius,StartMetrics::FooterActionRadius};
        for(const auto id:{L"BackgroundBorder",L"HighContrastBorder"})if(auto border=FindStartElement(root,id).try_as<sx::Controls::Border>()){
            parts_.Set(border,sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(radius));if(std::wstring_view(id)==L"BackgroundBorder")complete_=true;}
    }
    void Restore()noexcept{
        try{if(widthToken_)element_.UnregisterPropertyChangedCallback(sx::FrameworkElement::WidthProperty(),widthToken_);if(sizeToken_.value)element_.SizeChanged(sizeToken_);}catch(...){}
        widthToken_=0;sizeToken_={};parts_.Clear();values_.Clear();template_=nullptr;powerHost_=nullptr;complete_=applied_=false;
    }
    bool Applied()const noexcept{return applied_;}
};
}

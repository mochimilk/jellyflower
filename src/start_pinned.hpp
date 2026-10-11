// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <cmath>
#include <algorithm>
#include <winrt/Windows.UI.Xaml.Automation.h>
namespace acrylic {
inline bool IsStartPinnedTile(const sx::FrameworkElement& element,StartClassName name=nullptr) {
    if(!element)return false;const auto type=name?name(element):winrt::get_class_name(element);
    return type==L"StartMenu.PinnedListTile" || type==L"StartMenu.PinnedListCollapsedFolder"
        || type==L"StartDocked.PinnedListTile" || type==L"StartDocked.PinnedListCollapsedFolder";
}
// Captions remain in the native model, automation name and tooltip. Only their
// visible TextBlock is collapsed. Do not touch folder titles or rename fields.
class StartPinned {
    sx::FrameworkElement tile_;
    sx::Controls::Grid root_{nullptr};
    sx::Controls::TextBlock label_{nullptr};
    int64_t captionToken_=0;
    StartSearchValues values_;
    struct Identity {sx::FrameworkElement element;sx::DependencyProperty key;bool name;};
    std::vector<Identity> identities_;
    bool applied_=false;
    void CaptionChanged()noexcept{
        if(!applied_ || !label_)return;
        try{for(const auto& i:identities_){auto original=values_.Original(i.element,i.key);
            if((i.name && winrt::unbox_value<winrt::hstring>(original).empty()) || (!i.name && !original))original=winrt::box_value(label_.Text());
            values_.Update(i.element,i.key,original);}}catch(...){}
    }
    void IdentityFor(const sx::FrameworkElement& element,bool tooltip=false){
        const auto key=tooltip?sx::Controls::ToolTipService::ToolTipProperty():sx::Automation::AutomationProperties::NameProperty();
        if(tooltip?sx::Controls::ToolTipService::GetToolTip(element)!=nullptr:!sx::Automation::AutomationProperties::GetName(element).empty())return;
        values_.Set(element,key,nullptr,[this,tooltip](const auto& native){
            const bool missing=tooltip?!native:winrt::unbox_value_or<winrt::hstring>(native,L"").empty();
            return missing?winrt::box_value(label_.Text()):native;
        });identities_.push_back({element,key,!tooltip});
    }
public:
    explicit StartPinned(const sx::FrameworkElement& tile):tile_(tile){}
    ~StartPinned(){Restore();}
    StartPinned(const StartPinned&)=delete;StartPinned& operator=(const StartPinned&)=delete;
    void Refresh(bool enabled){
        if(!enabled){Restore();return;}
        const auto root=FindStartElement(tile_,L"Root").try_as<sx::Controls::Grid>();
        const auto alternative=root?root:FindStartElement(tile_,L"RootGrid").try_as<sx::Controls::Grid>();
        if(applied_ && alternative==root_ && label_ && sx::Media::VisualTreeHelper::GetParent(label_))return;
        Restore();root_=alternative;if(!root_)return;
        try {
            winrt::hstring caption;
            for(const auto id:{L"DisplayName",L"FolderDisplayName",L"FolderName"}) {
                if(auto label=FindStartElement(root_,id).try_as<sx::Controls::TextBlock>()) {
                    if(caption.empty()){caption=label.Text();label_=label;}
                    values_.Set(label,sx::UIElement::VisibilityProperty(),winrt::box_value(sx::Visibility::Collapsed));
                }
            }
            if(caption.empty())return; // Unknown folder template: retain its stock presentation.
            IdentityFor(tile_);IdentityFor(tile_,true);
            auto parent=sx::Media::VisualTreeHelper::GetParent(tile_);
            for(unsigned i=0;parent && i<12;++i) {
                if(auto item=parent.try_as<sx::Controls::GridViewItem>()) {
                    IdentityFor(item);
                    break;
                }
                parent=sx::Media::VisualTreeHelper::GetParent(parent);
            }
            values_.Set(root_,sx::Controls::Grid::PaddingProperty(),nullptr,[](const auto& value){auto p=winrt::unbox_value<sx::Thickness>(value);p.Top=p.Bottom=0;return winrt::box_value(p);});
            for(const auto id:{L"LogoContainer",L"LogosContainer",L"FolderHintPlate",L"FolderIcon",L"FolderIconContainer"}) {
                if(auto icon=FindStartElement(root_,id);icon && sx::Media::VisualTreeHelper::GetParent(icon)==root_) {
                    values_.Set(icon,sx::Controls::Grid::RowProperty(),winrt::box_value(0));
                    values_.Set(icon,sx::Controls::Grid::RowSpanProperty(),winrt::box_value(static_cast<int>(std::max(1u,root_.RowDefinitions().Size()))));
                    values_.Set(icon,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Center));
                    values_.Set(icon,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Center));
                    const double height=icon.Height();if(!std::isfinite(height) && icon.ActualHeight()>0)values_.Set(icon,sx::FrameworkElement::HeightProperty(),winrt::box_value(icon.ActualHeight()));
                }
            }
            applied_=true;
            captionToken_=label_.RegisterPropertyChangedCallback(sx::Controls::TextBlock::TextProperty(),[this](auto&&,auto&&){CaptionChanged();});
        }catch(...){Restore();throw;}
    }
    void Restore()noexcept{applied_=false;try{if(label_ && captionToken_)label_.UnregisterPropertyChangedCallback(sx::Controls::TextBlock::TextProperty(),captionToken_);}catch(...){}captionToken_=0;values_.Clear();identities_.clear();label_=nullptr;root_=nullptr;}
    bool Applied()const noexcept{return applied_;}
    sx::FrameworkElement Element()const{return tile_;}
};
}

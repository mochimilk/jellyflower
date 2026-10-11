// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_material.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <vector>

namespace acrylic {
namespace sx = winrt::Windows::UI::Xaml;
using StartClassName = winrt::hstring (*)(const sx::DependencyObject&);
inline bool IsStartSurface(const sx::Controls::Border& border, StartClassName name = nullptr) {
    if (!border || (border.Name()!=L"AcrylicBorder" && border.Name()!=L"AcrylicOverlay")) return false;
    auto current=sx::Media::VisualTreeHelper::GetParent(border);
    for(unsigned depth=0;current && depth<32;++depth) {
        const auto type=name ? name(current) : winrt::get_class_name(current);
        const std::wstring_view text(type);
        if(text.substr(0,12)==L"StartDocked." || text.substr(0,10)==L"StartMenu.") return true;
        current=sx::Media::VisualTreeHelper::GetParent(current);
    }
    return false;
}
// Save local values and bindings, including the effective AcrylicBorder brush
// (ReadLocalValue can return a different acrylic brush on this shell element).
class StartSurface {
    struct Property {
        sx::DependencyProperty key{nullptr};
        winrt::Windows::Foundation::IInspectable local{nullptr}, custom{nullptr};
        sx::Data::BindingBase binding{nullptr};
        int64_t token=0;
    };
    sx::Controls::Border border_;
    std::vector<Property> properties_;
    bool applied_=false, updating_=false, light_=false;
    void Save(Property& p) {
        p.local=border_.ReadLocalValue(p.key);
        const auto expression=border_.GetBindingExpression(p.key);
        p.binding=expression ? expression.ParentBinding() : nullptr;
        if(p.key==sx::Controls::Border::BackgroundProperty() && !p.binding
            && p.local!=sx::DependencyProperty::UnsetValue()) p.local=border_.GetValue(p.key);
    }
    void Changed(size_t index) noexcept {
        if(updating_ || !applied_) return;
        try {
            auto& p=properties_[index];
            if(winrt::get_abi(border_.GetValue(p.key))==winrt::get_abi(p.custom)) return;
            Save(p); updating_=true; border_.SetValue(p.key,p.custom); updating_=false;
        } catch(...) { updating_=false; }
    }
    void Add(const sx::DependencyProperty& key, const winrt::Windows::Foundation::IInspectable& custom) {
        const auto index=properties_.size();
        properties_.push_back({key,nullptr,custom,nullptr,0});
        properties_.back().token=border_.RegisterPropertyChangedCallback(key,[this,index](auto&&,auto&&){Changed(index);});
    }
public:
    explicit StartSurface(const sx::Controls::Border& border):border_(border) {
        if(!border_) winrt::throw_hresult(E_INVALIDARG);
        // A failed construction must unregister any installed callbacks.
        try {
            if(border_.Name()==L"AcrylicOverlay") Add(sx::UIElement::VisibilityProperty(),winrt::box_value(sx::Visibility::Collapsed));
            else {
                Add(sx::Controls::Border::BackgroundProperty(),nullptr);
                Add(sx::Controls::Border::BorderBrushProperty(),nullptr);
                Add(sx::Controls::Border::BorderThicknessProperty(),winrt::box_value(sx::Thickness{1,1,1,1}));
                Add(sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(sx::CornerRadius{8,8,8,8}));
            }
        } catch(...) {
            for(auto& p:properties_) if(p.token) border_.UnregisterPropertyChangedCallback(p.key,p.token);
            throw;
        }
    }
    ~StartSurface() {
        for(auto& p:properties_) try { border_.UnregisterPropertyChangedCallback(p.key,p.token); } catch(...) {}
        try { Restore(); } catch(...) {}
    }
    StartSurface(const StartSurface&)=delete;
    StartSurface& operator=(const StartSurface&)=delete;
    void Refresh(bool enabled,bool light) {
        if(!enabled) { Restore(); return; }
        if(applied_ && light_==light) return;
        updating_=true;
        try {
            if(border_.Name()!=L"AcrylicOverlay") {
                const auto ink=MenuBorder(light);
                properties_[0].custom=winrt::make<StartMaterial>(border_,light);
                properties_[1].custom=sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,GetRValue(ink),GetGValue(ink),GetBValue(ink)});
            }
            if(!applied_) for(auto& p:properties_) Save(p);
            applied_=true; light_=light;
            for(auto& p:properties_) border_.SetValue(p.key,p.custom);
            updating_=false;
        } catch(...) { updating_=false; Restore(); throw; }
    }
    void Restore() {
        if(!applied_) return;
        updating_=true;
        try {
            for(auto& p:properties_) {
                border_.ClearValue(p.key);
                if(p.binding) border_.SetBinding(p.key,p.binding);
                else if(p.local!=sx::DependencyProperty::UnsetValue()) border_.SetValue(p.key,p.local);
            }
            applied_=false; updating_=false;
        } catch(...) { updating_=false; throw; }
    }
    bool Applied() const noexcept { return applied_; }
};
}

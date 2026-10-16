// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "menu_material.hpp"
#include "composition_effect.hpp"
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <d2d1effects.h>

namespace acrylic {
namespace se=winrt::Windows::Graphics::Effects;
namespace sc=winrt::Windows::UI::Composition;
namespace sm=winrt::Windows::UI::Xaml::Media;
// A two-effect graph: blur the backdrop, then alpha-blend exactly one tint.
// AcrylicBrush's luminosity + tint stack has different opacity semantics from
// the native popup accent policy and can appear solid at the same numeric value.
struct StartEffect : winrt::implements<StartEffect,se::IGraphicsEffect,se::IGraphicsEffectSource,JellyEffectInterop> {
    bool tint;
    DWORD color;
    se::IGraphicsEffectSource source;
    winrt::hstring name;
    StartEffect(bool isTint,DWORD value,se::IGraphicsEffectSource input):tint(isTint),color(value),source(input){}
    winrt::hstring Name() const {return name;}
    void Name(winrt::hstring const& value) {name=value;}
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
        if(!id) return E_POINTER;*id=tint?CLSID_D2D1ColorMatrix:CLSID_D2D1GaussianBlur;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR,UINT*,UINT*) noexcept override {return E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override {if(!count)return E_POINTER;*count=3;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override {if(!count)return E_POINTER;*count=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSource(UINT index,IUnknown** value) noexcept override {
        if(!value)return E_POINTER;*value=nullptr;if(index)return E_BOUNDS;
        winrt::copy_to_abi(source,*reinterpret_cast<void**>(value));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index,winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>** value) noexcept override {
        if(!value)return E_POINTER;*value=nullptr;
        try {
            using winrt::Windows::Foundation::PropertyValue;
            winrt::Windows::Foundation::IInspectable property{nullptr};
            if(index>=3)return E_BOUNDS;
            if(!tint) property=index==0?PropertyValue::CreateSingle(25.f):PropertyValue::CreateUInt32(
                index==1?static_cast<UINT>(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED):static_cast<UINT>(D2D1_BORDER_MODE_HARD));
            else if(index==0) {
                const float alpha=(color>>24)/255.f,remain=1-alpha;
                const float matrix[]{remain,0,0,0, 0,remain,0,0, 0,0,remain,0, 0,0,0,1,
                    (color&255)/255.f*alpha,((color>>8)&255)/255.f*alpha,((color>>16)&255)/255.f*alpha,0};
                property=PropertyValue::CreateSingleArray(matrix);
            } else property=index==1?PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED):PropertyValue::CreateBoolean(true);
            winrt::copy_to_abi(property.as<winrt::Windows::Foundation::IPropertyValue>(),*reinterpret_cast<void**>(value));return S_OK;
        } catch(...) {return winrt::to_hresult();}
    }
};
struct StartMaterial : sm::XamlCompositionBrushBaseT<StartMaterial> {
    sc::Compositor compositor;
    DWORD tint;
    StartMaterial(winrt::Windows::UI::Xaml::UIElement const& element,bool light)
        :compositor(winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(element).Compositor()),tint(MenuTint(light)) {
        FallbackColor({255,static_cast<BYTE>(tint),static_cast<BYTE>(tint>>8),static_cast<BYTE>(tint>>16)});
    }
    void OnConnected() {
        if(CompositionBrush())return;
        auto blur=winrt::make<StartEffect>(false,0,sc::CompositionEffectSourceParameter(L"backdrop"));
        auto color=winrt::make<StartEffect>(true,tint,blur.as<se::IGraphicsEffectSource>());
        auto brush=compositor.CreateEffectFactory(color).CreateBrush();
        brush.SetSourceParameter(L"backdrop",compositor.CreateBackdropBrush());
        CompositionBrush(brush);
    }
    void OnDisconnected() {CompositionBrush(nullptr);}
};
}

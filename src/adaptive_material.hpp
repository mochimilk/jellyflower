// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "composition_effect.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <d2d1effects.h>

namespace acrylic::adaptive {
namespace effects=winrt::Windows::Graphics::Effects;
namespace composition=winrt::Windows::UI::Composition;
namespace media=winrt::Windows::UI::Xaml::Media;
enum class Stage { Linear, Average, Select };
struct Effect : winrt::implements<Effect,effects::IGraphicsEffect,effects::IGraphicsEffectSource,JellyEffectInterop> {
    Stage stage;
    effects::IGraphicsEffectSource source;
    winrt::hstring name;
    Effect(Stage value,effects::IGraphicsEffectSource input):stage(value),source(input){}
    winrt::hstring Name() const{return name;}
    void Name(winrt::hstring const& value){name=value;}
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
        if(!id)return E_POINTER;
        *id=stage==Stage::Linear?CLSID_D2D1GammaTransfer:stage==Stage::Average?CLSID_D2D1GaussianBlur:CLSID_D2D1ColorMatrix;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR,UINT*,UINT*) noexcept override{return E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override{if(!count)return E_POINTER;*count=stage==Stage::Linear?17:3;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override{if(!count)return E_POINTER;*count=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSource(UINT index,IUnknown** value) noexcept override{
        if(!value)return E_POINTER;*value=nullptr;if(index)return E_BOUNDS;
        winrt::copy_to_abi(source,*reinterpret_cast<void**>(value));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index,winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>** value) noexcept override {
        if(!value)return E_POINTER;*value=nullptr;
        if(index>=(stage==Stage::Linear?17u:3u))return E_BOUNDS;
        try{
            using winrt::Windows::Foundation::PropertyValue;
            winrt::Windows::Foundation::IInspectable property{nullptr};
            if(stage==Stage::Linear){
                // Approximate sRGB linearization before spatial averaging. Alpha
                // stays untouched. This also gives saturated backgrounds proper
                // luminance weighting, unlike averaging encoded RGB directly.
                if(index==16 || index%4==3)property=PropertyValue::CreateBoolean(index==16 || index==15);
                else property=PropertyValue::CreateSingle(index%4==0?1.f:index%4==1?2.2f:0.f);
            }else if(stage==Stage::Average){
                property=index==0?PropertyValue::CreateSingle(16.f):PropertyValue::CreateUInt32(index==1?D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED:D2D1_BORDER_MODE_HARD);
            }else if(index==0){
                // Black/white contrast is equal at luminance sqrt(.0525)-.05.
                // Clamp a steep matrix to choose ink, including over mid-gray.
                // The local Gaussian is a weighted neighborhood, not an exact
                // corner-wide mean, and adds no CPU capture/readback or timer.
                constexpr float slope=65536.f,cutoff=.1791288f;
                const float matrix[]{-.2126f*slope,-.2126f*slope,-.2126f*slope,0,
                    -.7152f*slope,-.7152f*slope,-.7152f*slope,0,
                    -.0722f*slope,-.0722f*slope,-.0722f*slope,0,
                    0,0,0,0,cutoff*slope,cutoff*slope,cutoff*slope,1};
                property=PropertyValue::CreateSingleArray(matrix);
            }else property=index==1?PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT):PropertyValue::CreateBoolean(true);
            winrt::copy_to_abi(property.as<winrt::Windows::Foundation::IPropertyValue>(),*reinterpret_cast<void**>(value));return S_OK;
        }catch(...){return winrt::to_hresult();}
    }
};
inline composition::CompositionEffectBrush CreateInk(composition::Compositor const& compositor){
    struct Cache{composition::Compositor compositor{nullptr};composition::CompositionEffectFactory factory{nullptr};};
    // UI-thread/Explorer lifetime storage avoids releasing XAML/Composition
    // objects in TLS teardown under the loader lock. One shader factory is
    // shared by all targets on this compositor, including bitmap masks.
    static thread_local auto* cache=new Cache;
    if(cache->compositor!=compositor || !cache->factory){
        auto linear=winrt::make<Effect>(Stage::Linear,composition::CompositionEffectSourceParameter(L"backdrop"));
        auto average=winrt::make<Effect>(Stage::Average,linear.as<effects::IGraphicsEffectSource>());
        auto select=winrt::make<Effect>(Stage::Select,average.as<effects::IGraphicsEffectSource>());
        cache->factory=compositor.CreateEffectFactory(select);cache->compositor=compositor;
    }
    auto ink=cache->factory.CreateBrush();ink.SetSourceParameter(L"backdrop",compositor.CreateBackdropBrush());return ink;
}
struct Ink : media::XamlCompositionBrushBaseT<Ink> {
    composition::CompositionBrush ready{nullptr};
    Ink(winrt::Windows::UI::Xaml::UIElement const& element,winrt::Windows::UI::Color fallback,composition::CompositionBrush const& mask=nullptr){
        const auto compositor=winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(element).Compositor();
        const auto ink=CreateInk(compositor);
        if(mask){auto masked=compositor.CreateMaskBrush();masked.Source(ink);masked.Mask(mask);ready=masked;}else ready=ink;
        FallbackColor(fallback);
    }
    void OnConnected() noexcept {try{CompositionBrush(ready);}catch(...){}}
    void OnDisconnected() noexcept {try{CompositionBrush(nullptr);}catch(...){}}
};
}

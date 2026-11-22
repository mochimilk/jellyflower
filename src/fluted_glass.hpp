// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "composition_effect.hpp"
#include "fluted_reeds.hpp"
#include <d2d1effects.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <cmath>
#include <cwchar>
#include <utility>
#include <vector>

namespace acrylic {
namespace fluted {
namespace composition = winrt::Windows::UI::Composition;
namespace effects = winrt::Windows::Graphics::Effects;
using Source = effects::IGraphicsEffectSource;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::PropertyValue;

struct Effect : winrt::implements<Effect, effects::IGraphicsEffect, Source, JellyEffectInterop> {
    GUID id;
    winrt::hstring name;
    std::vector<Source> sources;
    std::vector<IInspectable> properties;
    Effect(GUID effect, winrt::hstring label, std::vector<Source> inputs, std::vector<IInspectable> values)
        : id(effect), name(std::move(label)), sources(std::move(inputs)), properties(std::move(values)) {}
    winrt::hstring Name() const { return name; }
    void Name(winrt::hstring const& value) { name = value; }
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* out) noexcept override { *out = id; return S_OK; }
    // A transform's matrix is animatable, so one factory serves every reed.
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR property, UINT* index, UINT* mapping) noexcept override {
        if (id != CLSID_D2D12DAffineTransform || std::wcscmp(property, L"TransformMatrix") != 0) return E_INVALIDARG;
        *index = D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX;
        *mapping = 1; // GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override { *count = static_cast<UINT>(properties.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index, winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>** value) noexcept override {
        if (index >= properties.size()) return E_BOUNDS;
        winrt::copy_to_abi(properties[index].as<winrt::Windows::Foundation::IPropertyValue>(), *reinterpret_cast<void**>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override { *count = static_cast<UINT>(sources.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSource(UINT index, IUnknown** value) noexcept override {
        if (index >= sources.size()) return E_BOUNDS;
        winrt::copy_to_abi(sources[index], *reinterpret_cast<void**>(value));
        return S_OK;
    }
};
inline Source Make(GUID id, const wchar_t* name, std::vector<Source> inputs, std::vector<IInspectable> values) {
    return winrt::make<Effect>(id, name, std::move(inputs), std::move(values)).as<Source>();
}
inline IInspectable Floats(std::vector<float> values) { return PropertyValue::CreateSingleArray(values); }
inline Source Channels(Source input, std::vector<float> matrix) {
    return Make(CLSID_D2D1ColorMatrix, L"", {std::move(input)}, {Floats(std::move(matrix)),
        PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED), PropertyValue::CreateBoolean(true)});
}
inline Source Arithmetic(Source a, Source b, std::vector<float> k) {
    return Make(CLSID_D2D1ArithmeticComposite, L"", {std::move(a), std::move(b)}, {Floats(std::move(k)), PropertyValue::CreateBoolean(true)});
}
// An effect graph must be a tree, so each bend reads its own copy of the page.
inline Source Bend(const wchar_t* name, const wchar_t* page) {
    return Make(CLSID_D2D12DAffineTransform, name, {composition::CompositionEffectSourceParameter(page)}, {
        PropertyValue::CreateUInt32(D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR), PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD),
        Floats({1, 0, 0, 1, 0, 0}), PropertyValue::CreateSingle(0.f)});
}
// A reed's lens draws the page at c - k (x - c) at x.
inline winrt::Windows::Foundation::Numerics::float3x2 Lens(double center, double k) {
    return {static_cast<float>(-1 / k), 0, 0, 1, static_cast<float>((1 + k) * center / k), 0};
}
}

// The reeds' effect, created once per compositor and shared by every glass on it.
inline winrt::Windows::UI::Composition::CompositionEffectFactory CreateFlutedEffect(const winrt::Windows::UI::Composition::Compositor& compositor) {
    using namespace fluted;
    // Red keeps its own bend and blue its own; green is their average.
    const auto red = Channels(Bend(L"red", L"redPage"), {1, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0});
    const auto blue = Channels(Bend(L"blue", L"bluePage"), {0, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0});
    // Reflecting a share of what's there, not of white, leaves a dark backdrop dark.
    const auto lit = Arithmetic(Arithmetic(red, blue, {0, 1, 1, 0}), composition::CompositionEffectSourceParameter(L"sheen"), {1, 1, 0, 0});
    return compositor.CreateEffectFactory(lit.as<effects::IGraphicsEffect>(), std::vector<winrt::hstring>{L"red.TransformMatrix", L"blue.TransformMatrix"});
}

// Fluted glass over a page brush (a host backdrop: what's behind the window, blurred by Windows), seen
// through vertical reeds, with a tint on top. A backdrop brush can't be transformed, so the page is drawn
// once into a visual surface and every reed's lens reads that; the page's visual must stay in the live
// tree, or the surface reads black. Root() goes wherever the glass belongs: a window target or an
// element's child visual.
class FlutedGlass {
public:
    FlutedGlass(const winrt::Windows::UI::Composition::Compositor& compositor, const winrt::Windows::UI::Composition::CompositionEffectFactory& effect,
        const winrt::Windows::UI::Composition::CompositionBrush& page, winrt::Windows::UI::Color tint)
        : compositor_(compositor), effect_(effect), root_(compositor.CreateContainerVisual()), reeds_(compositor.CreateContainerVisual()),
          page_(compositor.CreateSpriteVisual()), tintVisual_(compositor.CreateSpriteVisual()), tint_(compositor.CreateColorBrush(tint)),
          surface_(compositor.CreateVisualSurface()), sheen_(compositor.CreateLinearGradientBrush()) {
        page_.Brush(page);
        surface_.SourceVisual(page_);
        pageBrush_ = compositor.CreateSurfaceBrush(surface_);
        sheen_.MappingMode(winrt::Windows::UI::Composition::CompositionMappingMode::Absolute);
        sheen_.ExtendMode(winrt::Windows::UI::Composition::CompositionGradientExtendMode::Wrap);
        for (const auto& [offset, share] : ReedSheen) {
            const auto v = static_cast<uint8_t>(std::lround(share * 255));
            sheen_.ColorStops().Append(compositor.CreateColorGradientStop(offset, {255, v, v, v}));
        }
        tintVisual_.Brush(tint_);
        root_.Children().InsertAtTop(page_);
        root_.Children().InsertAtTop(reeds_);
        root_.Children().InsertAtTop(tintVisual_);
    }
    const winrt::Windows::UI::Composition::ContainerVisual& Root() const noexcept { return root_; }
    // Lays the reeds across `size`, in pixels at `dpi`, or in DIPs at 96. Corners round the glass's clip;
    // 0 leaves it square.
    void Layout(winrt::Windows::Foundation::Numerics::float2 size, unsigned dpi, float corner = 0) {
        if (size == size_ && dpi == dpi_ && corner == corner_) return;
        if (size != size_ || dpi != dpi_) {
            for (auto visual : {root_, reeds_}) visual.Size(size);
            page_.Size(size);
            tintVisual_.Size(size);
            surface_.SourceSize(size);
            reeds_.Children().RemoveAll();
            const auto reeds = FlutedReeds(size.x, dpi);
            if (!reeds.empty()) sheen_.EndPoint({static_cast<float>(reeds.front().width), 0});
            for (const auto& reed : reeds) {
                const double center = reed.left + reed.width / 2;
                const auto brush = effect_.CreateBrush();
                brush.SetSourceParameter(L"redPage", pageBrush_);
                brush.SetSourceParameter(L"bluePage", pageBrush_);
                brush.SetSourceParameter(L"sheen", sheen_);
                brush.Properties().InsertMatrix3x2(L"red.TransformMatrix", fluted::Lens(center, reed.red));
                brush.Properties().InsertMatrix3x2(L"blue.TransformMatrix", fluted::Lens(center, reed.blue));
                const auto visual = compositor_.CreateSpriteVisual();
                visual.Size(size);
                visual.Brush(brush);
                visual.Clip(compositor_.CreateInsetClip(static_cast<float>(reed.left), 0, static_cast<float>(size.x - (reed.left + reed.width)), 0));
                reeds_.Children().InsertAtTop(visual);
            }
        }
        if (corner > 0) {
            if (!shape_) {
                shape_ = compositor_.CreateRoundedRectangleGeometry();
                root_.Clip(compositor_.CreateGeometricClip(shape_));
            }
            shape_.Size(size);
            shape_.CornerRadius({corner, corner});
        } else if (shape_) {
            root_.Clip(nullptr);
            shape_ = nullptr;
        }
        size_ = size;
        dpi_ = dpi;
        corner_ = corner;
    }
    void Tint(winrt::Windows::UI::Color color) { tint_.Color(color); }
    // A new page brush. A host backdrop is a snapshot unless its window is active, and a new one takes a
    // new snapshot.
    void Page(const winrt::Windows::UI::Composition::CompositionBrush& page) { page_.Brush(page); }
private:
    winrt::Windows::UI::Composition::Compositor compositor_;
    winrt::Windows::UI::Composition::CompositionEffectFactory effect_;
    winrt::Windows::UI::Composition::ContainerVisual root_, reeds_;
    winrt::Windows::UI::Composition::SpriteVisual page_, tintVisual_;
    winrt::Windows::UI::Composition::CompositionColorBrush tint_;
    winrt::Windows::UI::Composition::CompositionVisualSurface surface_;
    winrt::Windows::UI::Composition::CompositionSurfaceBrush pageBrush_{nullptr};
    winrt::Windows::UI::Composition::CompositionLinearGradientBrush sheen_;
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry shape_{nullptr};
    winrt::Windows::Foundation::Numerics::float2 size_{};
    unsigned dpi_ = 0;
    float corner_ = 0;
};

}

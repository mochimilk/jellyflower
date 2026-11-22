// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_glass.hpp"
#include "composition_effect.hpp"
#include "menu_material.hpp"
#include "menu_reeds.hpp"
#include <dwmapi.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>
#include <cmath>
#include <cwchar>
#include <utility>
#include <vector>

namespace acrylic {
namespace {
namespace composition = winrt::Windows::UI::Composition;
namespace effects = winrt::Windows::Graphics::Effects;
namespace numerics = winrt::Windows::Foundation::Numerics;
namespace abi = ABI::Windows::UI::Composition;
using Source = effects::IGraphicsEffectSource;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::PropertyValue;

constexpr auto UseHostBackdrop = static_cast<DWMWINDOWATTRIBUTE>(17);
HRESULT lastResult = S_OK;

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
Source Make(GUID id, const wchar_t* name, std::vector<Source> inputs, std::vector<IInspectable> values) {
    return winrt::make<Effect>(id, name, std::move(inputs), std::move(values)).as<Source>();
}
IInspectable Floats(std::vector<float> values) { return PropertyValue::CreateSingleArray(values); }
Source Channels(Source input, std::vector<float> matrix) {
    return Make(CLSID_D2D1ColorMatrix, L"", {std::move(input)}, {Floats(std::move(matrix)),
        PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED), PropertyValue::CreateBoolean(true)});
}
Source Arithmetic(Source a, Source b, std::vector<float> k) {
    return Make(CLSID_D2D1ArithmeticComposite, L"", {std::move(a), std::move(b)}, {Floats(std::move(k)), PropertyValue::CreateBoolean(true)});
}
// A backdrop brush can't be transformed and an effect graph must be a tree, so each bend reads its own
// copy of the backdrop, drawn into a visual surface.
Source Bend(const wchar_t* name, const wchar_t* page) {
    return Make(CLSID_D2D12DAffineTransform, name, {composition::CompositionEffectSourceParameter(page)}, {
        PropertyValue::CreateUInt32(D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR), PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD),
        Floats({1, 0, 0, 1, 0, 0}), PropertyValue::CreateSingle(0.f)});
}
// A reed's lens maps the backdrop at c - k (x - c) to x.
numerics::float3x2 Lens(double center, double k) {
    return {static_cast<float>(-1 / k), 0, 0, 1, static_cast<float>((1 + k) * center / k), 0};
}
winrt::Windows::UI::Color Tint(bool light) {
    const DWORD c = MenuTint(light);
    return {static_cast<uint8_t>(c >> 24), static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)};
}

// One compositor, device and reed effect for the controller's thread, kept for the process, because
// creating a device for every menu would delay it opening. Deliberately leaked, so nothing is released
// after the apartment shuts down.
struct Shared {
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    composition::Compositor compositor{nullptr};
    composition::CompositionEffectFactory reeds{nullptr};
    winrt::com_ptr<ID3D11DeviceContext> context;
    composition::CompositionGraphicsDevice graphics{nullptr};
};
Shared& State() {
    static auto* shared = new Shared;
    return *shared;
}
Shared& Ready() {
    auto& s = State();
    if (!s.queue && !winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
        const DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(s.queue))));
    }
    if (!s.compositor) s.compositor = composition::Compositor();
    if (!s.reeds) {
        // Red keeps its own bend and blue its own; green is their average.
        const auto red = Channels(Bend(L"red", L"redPage"), {1, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0});
        const auto blue = Channels(Bend(L"blue", L"bluePage"), {0, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0});
        // Reflecting a share of what's there, not of white, leaves a dark backdrop dark.
        const auto lit = Arithmetic(Arithmetic(red, blue, {0, 1, 1, 0}), composition::CompositionEffectSourceParameter(L"sheen"), {1, 1, 0, 0});
        s.reeds = s.compositor.CreateEffectFactory(lit.as<effects::IGraphicsEffect>(),
            std::vector<winrt::hstring>{L"red.TransformMatrix", L"blue.TransformMatrix"});
    }
    if (!s.graphics) {
        winrt::com_ptr<ID3D11Device> device;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, s.context.put()));
        winrt::check_hresult(s.compositor.as<abi::ICompositorInterop>()->CreateGraphicsDevice(device.get(),
            reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(s.graphics))));
    }
    return s;
}
// A lost or failed device is created again for the next menu.
bool Failed(HRESULT result) {
    lastResult = result;
    auto& s = State();
    s.graphics = nullptr;
    s.context = nullptr;
    return false;
}
bool EnergySaver() {
    SYSTEM_POWER_STATUS power{};
    return GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
}
}

struct MenuGlass::Impl {
    composition::Desktop::DesktopWindowTarget target{nullptr};
    composition::CompositionColorBrush tint{nullptr};
    composition::CompositionDrawingSurface content{nullptr};
};

MenuGlass::MenuGlass() = default;
MenuGlass::~MenuGlass() = default;

bool MenuGlass::Attach(HWND window, UINT dpi, bool light) {
    if (impl_) { impl_->tint.Color(Tint(light)); return true; }
    // Windows turns its own materials opaque to save energy; the glass follows it.
    RECT client{};
    const BOOL on = TRUE;
    if (EnergySaver() || !GetClientRect(window, &client) || IsRectEmpty(&client)) return false;
    try {
        winrt::check_hresult(DwmSetWindowAttribute(window, UseHostBackdrop, &on, sizeof(on)));
        auto& s = Ready();
        auto impl = std::make_unique<Impl>();
        winrt::check_hresult(s.compositor.as<abi::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(window, FALSE,
            reinterpret_cast<abi::Desktop::IDesktopWindowTarget**>(winrt::put_abi(impl->target))));
        const float width = static_cast<float>(client.right), height = static_cast<float>(client.bottom);
        const numerics::float2 size{width, height};
        const auto root = s.compositor.CreateContainerVisual();
        root.RelativeSizeAdjustment({1, 1});
        const auto layer = [&](const composition::CompositionBrush& brush) {
            const auto visual = s.compositor.CreateSpriteVisual();
            visual.Size(size);
            visual.Brush(brush);
            root.Children().InsertAtTop(visual);
            return visual;
        };
        // The backdrop must be in the tree for its visual surface to see it.
        const auto backdrop = layer(s.compositor.CreateHostBackdropBrush());
        const auto page = s.compositor.CreateVisualSurface();
        page.SourceVisual(backdrop);
        page.SourceSize(size);
        const auto pageBrush = s.compositor.CreateSurfaceBrush(page);
        const auto reeds = MenuReeds(width, dpi);
        const auto sheen = s.compositor.CreateLinearGradientBrush();
        sheen.MappingMode(composition::CompositionMappingMode::Absolute);
        sheen.EndPoint({static_cast<float>(reeds.front().width), 0});
        sheen.ExtendMode(composition::CompositionGradientExtendMode::Wrap);
        for (const auto& [offset, share] : ReedSheen) {
            const auto v = static_cast<uint8_t>(std::lround(share * 255));
            sheen.ColorStops().Append(s.compositor.CreateColorGradientStop(offset, {255, v, v, v}));
        }
        for (const auto& reed : reeds) {
            const double center = reed.left + reed.width / 2;
            const auto brush = s.reeds.CreateBrush();
            brush.SetSourceParameter(L"redPage", pageBrush);
            brush.SetSourceParameter(L"bluePage", pageBrush);
            brush.SetSourceParameter(L"sheen", sheen);
            brush.Properties().InsertMatrix3x2(L"red.TransformMatrix", Lens(center, reed.red));
            brush.Properties().InsertMatrix3x2(L"blue.TransformMatrix", Lens(center, reed.blue));
            layer(brush).Clip(s.compositor.CreateInsetClip(static_cast<float>(reed.left), 0,
                static_cast<float>(width - (reed.left + reed.width)), 0));
        }
        impl->tint = s.compositor.CreateColorBrush(Tint(light));
        layer(impl->tint);
        impl->content = s.graphics.CreateDrawingSurface({width, height}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        layer(s.compositor.CreateSurfaceBrush(impl->content));
        impl->target.Root(root);
        impl_ = std::move(impl);
        lastResult = S_OK;
        return true;
    } catch (...) {
        return Failed(winrt::to_hresult());
    }
}

bool MenuGlass::Present(const RECT& bounds, const DWORD* pixels) {
    const auto& context = State().context;
    if (!impl_ || !context) return false;
    try {
        const auto surface = impl_->content.as<abi::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(&bounds, __uuidof(ID3D11Texture2D), texture.put_void(), &offset));
        const UINT width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        const D3D11_BOX box{static_cast<UINT>(offset.x), static_cast<UINT>(offset.y), 0, offset.x + width, offset.y + height, 1};
        context->UpdateSubresource(texture.get(), 0, &box, pixels, width * 4, 0);
        winrt::check_hresult(surface->EndDraw());
        return true;
    } catch (...) {
        return Failed(winrt::to_hresult());
    }
}

void MenuGlass::Detach() { impl_.reset(); }

HRESULT MenuGlass::LastResult() { return lastResult; }
}

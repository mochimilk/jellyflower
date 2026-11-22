// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_glass.hpp"
#include "fluted_glass.hpp"
#include "glass_compositor.hpp"
#include "glass_state.hpp"
#include "menu_material.hpp"
#include <dwmapi.h>
#include <d3d11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>

namespace acrylic {
namespace {
namespace composition = winrt::Windows::UI::Composition;
namespace abi = ABI::Windows::UI::Composition;

constexpr auto UseHostBackdrop = static_cast<DWMWINDOWATTRIBUTE>(17);
HRESULT lastResult = S_OK;

winrt::Windows::UI::Color Tint(bool light) {
    const DWORD c = MenuTint(light);
    return {static_cast<uint8_t>(c >> 24), static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)};
}

// The device the menu's content is copied with, kept for the process like the compositor.
struct Device {
    winrt::com_ptr<ID3D11DeviceContext> context;
    composition::CompositionGraphicsDevice graphics{nullptr};
};
Device& State() {
    static auto* device = new Device;
    return *device;
}
Device& Ready() {
    auto& d = State();
    if (!d.graphics) {
        winrt::com_ptr<ID3D11Device> device;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, d.context.put()));
        winrt::check_hresult(GlassCompositor().as<abi::ICompositorInterop>()->CreateGraphicsDevice(device.get(),
            reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(d.graphics))));
    }
    return d;
}
// A lost or failed device is created again for the next menu.
bool Lost(HRESULT result) {
    lastResult = result;
    auto& d = State();
    d.graphics = nullptr;
    d.context = nullptr;
    return false;
}
}

struct MenuGlass::Impl {
    composition::Desktop::DesktopWindowTarget target{nullptr};
    std::unique_ptr<FlutedGlass> glass;
    composition::CompositionDrawingSurface content{nullptr};
};

MenuGlass::MenuGlass() = default;
MenuGlass::~MenuGlass() = default;

bool MenuGlass::Attach(HWND window, UINT dpi, bool light) {
    if (impl_) { impl_->glass->Tint(Tint(light)); return true; }
    RECT client{};
    const BOOL on = TRUE;
    if (!FlutedGlassAllowed() || !GetClientRect(window, &client) || IsRectEmpty(&client)) return false;
    try {
        winrt::check_hresult(DwmSetWindowAttribute(window, UseHostBackdrop, &on, sizeof(on)));
        const auto compositor = GlassCompositor();
        auto& d = Ready();
        auto impl = std::make_unique<Impl>();
        winrt::check_hresult(compositor.as<abi::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(window, FALSE,
            reinterpret_cast<abi::Desktop::IDesktopWindowTarget**>(winrt::put_abi(impl->target))));
        const winrt::Windows::Foundation::Numerics::float2 size{static_cast<float>(client.right), static_cast<float>(client.bottom)};
        // The host backdrop arrives blurred by Windows.
        impl->glass = std::make_unique<FlutedGlass>(compositor, GlassReeds(), compositor.CreateHostBackdropBrush(), Tint(light));
        impl->glass->Layout(size, dpi);
        impl->content = d.graphics.CreateDrawingSurface({size.x, size.y}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        const auto content = compositor.CreateSpriteVisual();
        content.Size(size);
        content.Brush(compositor.CreateSurfaceBrush(impl->content));
        impl->glass->Root().Children().InsertAtTop(content);
        impl->target.Root(impl->glass->Root());
        impl_ = std::move(impl);
        lastResult = S_OK;
        return true;
    } catch (...) {
        return Lost(winrt::to_hresult());
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
        return Lost(winrt::to_hresult());
    }
}

void MenuGlass::Detach() { impl_.reset(); }

HRESULT MenuGlass::LastResult() { return lastResult; }
}

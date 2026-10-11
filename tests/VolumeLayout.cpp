// SPDX-License-Identifier: GPL-3.0-or-later
#include <unknwn.h>
#include "../src/volume.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <unordered_map>
#include <cstdio>
#include <stdexcept>
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
std::unordered_map<void*, winrt::hstring> names;
winrt::hstring Name(const xaml::DependencyObject& object) {
    const auto it = names.find(winrt::get_abi(object));
    return it == names.end() ? winrt::get_class_name(object) : it->second;
}
void Alias(const xaml::DependencyObject& object, const wchar_t* value) { names[winrt::get_abi(object)] = value; }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
struct Slot {
    controls::Grid presenter, icon, outer, inner;
    controls::TextBlock glyph;
    explicit Slot(wchar_t symbol) {
        Alias(presenter, L"Windows.UI.Xaml.Controls.ContentPresenter"); Alias(icon, L"SystemTray.IconView");
        glyph.Text(winrt::hstring(&symbol, 1)); glyph.FontSize(16);
        inner.MinWidth(24); inner.Children().Append(glyph); outer.Children().Append(inner);
        icon.Children().Append(outer); presenter.Children().Append(icon);
    }
};
void Run() {
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Volume layout fixture",
        WS_POPUP, 0, 0, 400, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "host window");
    xaml::Hosting::DesktopWindowXamlSource source;
    auto native = source.as<IDesktopWindowXamlSourceNative>(); winrt::check_hresult(native->AttachToWindow(host));
    HWND island = nullptr; winrt::check_hresult(native->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 400, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    {
        controls::Button button; button.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        int clicks = 0; const auto token = button.Click([&](auto&&, auto&&) { ++clicks; });
        controls::Grid root, frame, content, items;
        root.Name(L"ControlCenterButton"); Alias(root, L"SystemTray.OmniButton");
        Alias(content, L"Windows.UI.Xaml.Controls.ContentPresenter"); Alias(items, L"Windows.UI.Xaml.Controls.ItemsPresenter");
        controls::StackPanel stack; stack.Orientation(controls::Orientation::Horizontal);
        Slot network(0xE701), volume(0xE995), battery(0xE83F);
        stack.Children().Append(network.presenter); stack.Children().Append(volume.presenter); stack.Children().Append(battery.presenter);
        items.Children().Append(stack); content.Children().Append(items); frame.Children().Append(content); root.Children().Append(frame);
        button.Content(root); source.Content(button); Pump(); button.UpdateLayout();
        Check(acrylic::FindVolumeRoot(volume.glyph, Name) == root, "find exact Quick Settings root");
        Check(acrylic::FindVolumeGrid(root, Name) == volume.inner, "recognized speaker target");
        Check(!acrylic::FindVolumeGrid(root), "ordinary app controls rejected without test metadata");
        const auto before = stack.ActualWidth();
        const auto unset = xaml::DependencyProperty::UnsetValue();
        {
            acrylic::VolumeView view(volume.inner); view.Refresh(true); button.UpdateLayout();
            Check(view.Applied() && volume.inner.Visibility() == xaml::Visibility::Collapsed, "speaker collapsed");
            Check(network.inner.Visibility() == xaml::Visibility::Visible && battery.inner.Visibility() == xaml::Visibility::Visible, "network and battery preserved");
            Check(stack.ActualWidth() <= before - 23, "speaker slot leaves no empty space");
            Check(button.Visibility() == xaml::Visibility::Visible && root.IsHitTestVisible(), "Quick Settings button preserved");
            auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);
            peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke).as<xaml::Automation::Provider::IInvokeProvider>().Invoke();
            Pump(); Check(clicks == 1, "stock Quick Settings click handler preserved");
            for (wchar_t symbol : {wchar_t(0xE74F), wchar_t(0xE767), wchar_t(0xE992), wchar_t(0xE993), wchar_t(0xE994), wchar_t(0xE995)}) {
                volume.glyph.Text(winrt::hstring(&symbol, 1));
                Check(acrylic::FindVolumeGrid(root, Name) == volume.inner, "mute and changing volume glyphs recognized");
            }
            for (int i = 0; i < 100; ++i) { volume.inner.Visibility(xaml::Visibility::Visible); view.Refresh(true); }
            Check(volume.inner.Visibility() == xaml::Visibility::Collapsed, "shell visibility writes immediately reapplied");
            view.Restore(); button.UpdateLayout();
            Check(volume.inner.Visibility() == xaml::Visibility::Visible && stack.ActualWidth() == before, "exit restores speaker and width");
        }
        volume.inner.ClearValue(xaml::UIElement::VisibilityProperty());
        { acrylic::VolumeView view(volume.inner); view.Refresh(true); }
        Check(volume.inner.ReadLocalValue(xaml::UIElement::VisibilityProperty()) == unset, "unset visibility restored");
        xaml::Data::Binding binding; binding.Source(winrt::box_value(xaml::Visibility::Visible));
        volume.inner.SetBinding(xaml::UIElement::VisibilityProperty(), binding);
        { acrylic::VolumeView view(volume.inner); view.Refresh(true); view.Refresh(false); }
        const auto expression = volume.inner.GetBindingExpression(xaml::UIElement::VisibilityProperty());
        Check(expression && expression.ParentBinding() == binding, "stock visibility binding restored");
        volume.glyph.Text(L"\uE701"); Check(!acrylic::FindVolumeGrid(root, Name), "wrong second-slot glyph rejected");
        volume.glyph.Text(L""); Check(!acrylic::FindVolumeGrid(root, Name), "late glyph not guessed");
        volume.glyph.Text(L"\uE995"); Check(acrylic::FindVolumeGrid(root, Name) == volume.inner, "late glyph discovered on retry");
        root.Name(L"NotificationCenterButton"); Check(!acrylic::FindVolumeGrid(root, Name), "notification center rejected");
        root.Name(L"ControlCenterButton"); stack.Children().RemoveAt(0);
        Check(!acrylic::FindVolumeGrid(root, Name), "reordered shell slots rejected");
        stack.Children().InsertAt(0, network.presenter); stack.Children().RemoveAt(2);
        Check(acrylic::FindVolumeGrid(root, Name) == volume.inner, "desktop layout without battery supported");
        button.Click(token); source.Content(nullptr); names.clear();
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: native XAML speaker selection/measurement, network/battery/click preservation, mute/late glyphs, 100 shell writes, unset/binding/exit restoration, unsupported layout rejection.");
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        Run(); manager.Close(); Pump(); return 0;
    } catch (const winrt::hresult_error& e) { std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(e.code())); std::fwprintf(stderr, L"%s\n", e.message().c_str()); return 1; }
    catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
}

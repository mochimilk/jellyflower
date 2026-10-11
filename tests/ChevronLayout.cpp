// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/chevron.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <unordered_map>
#include <cstdio>
#include <cmath>
#include <stdexcept>
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
std::unordered_map<void*, winrt::hstring> names;
winrt::hstring Name(const xaml::DependencyObject& object) {
    const auto found = names.find(winrt::get_abi(object));
    return found == names.end() ? winrt::get_class_name(object) : found->second;
}
void Alias(const xaml::DependencyObject& object, const wchar_t* value) { names[winrt::get_abi(object)] = value; }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
bool Equal(double a, double b) { return std::abs(a - b) < 0.0001; }
void Run() {
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Chevron layout fixture",
        WS_POPUP, 0, 0, 400, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "host window");
    xaml::Hosting::DesktopWindowXamlSource source;
    auto native = source.as<IDesktopWindowXamlSourceNative>(); winrt::check_hresult(native->AttachToWindow(host));
    HWND island = nullptr; winrt::check_hresult(native->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 400, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    {
        controls::Button button; button.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        int clicks = 0; const auto click = button.Click([&](auto&&, auto&&) { ++clicks; });
        const auto tip = winrt::box_value(L"Show hidden icons"); controls::ToolTipService::SetToolTip(button, tip);
        controls::Grid root, container, presenter, content, textIcon, innerContainer, adaptive;
        Alias(root, L"SystemTray.ChevronIconView"); container.Name(L"ContainerGrid");
        Alias(presenter, L"Windows.UI.Xaml.Controls.ContentPresenter"); presenter.Name(L"ContentPresenter");
        content.Name(L"ContentGrid"); Alias(textIcon, L"SystemTray.TextIconContent");
        innerContainer.Name(L"ContainerGrid"); adaptive.Name(L"Base"); Alias(adaptive, L"SystemTray.AdaptiveTextBlock");
        controls::TextBlock glyph; glyph.Name(L"InnerTextBlock"); glyph.Text(L"\uE70E"); glyph.FontSize(16);
        adaptive.Children().Append(glyph); innerContainer.Children().Append(adaptive);
        textIcon.Children().Append(innerContainer); content.Children().Append(textIcon);
        presenter.Children().Append(content); container.Children().Append(presenter); root.Children().Append(container);
        controls::TextBlock neighbor; neighbor.Text(L"other icon"); neighbor.Opacity(0.9); root.Children().Append(neighbor);
        button.Content(root); source.Content(button); Pump(); button.UpdateLayout();
        Check(acrylic::FindChevronRoot(glyph, Name) == root, "find exact chevron root");
        Check(acrylic::FindChevronGlyph(root, Name) == glyph, "find exact chevron glyph");
        Check(!acrylic::FindChevronGlyph(root), "ordinary controls rejected without shell metadata");
        const auto width = button.ActualWidth(), height = button.ActualHeight();
        const auto unset = xaml::DependencyProperty::UnsetValue();
        {
            acrylic::ChevronView view(glyph); view.Refresh(true); button.UpdateLayout();
            Check(Equal(glyph.Opacity(), 0.6), "glyph is 60 percent opaque");
            Check(Equal(root.Opacity(), 1) && Equal(button.Opacity(), 1) && Equal(neighbor.Opacity(), 0.9), "only glyph dimmed");
            Check(Equal(width, button.ActualWidth()) && Equal(height, button.ActualHeight()) && button.IsHitTestVisible(), "button geometry and hit testing preserved");
            Check(controls::ToolTipService::GetToolTip(button) == tip, "stock tooltip preserved");
            auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);
            peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke).as<xaml::Automation::Provider::IInvokeProvider>().Invoke();
            Pump(); Check(clicks == 1, "stock overflow button handler preserved");
            glyph.Text(L"\uE70D"); Check(Equal(glyph.Opacity(), 0.6), "chevron direction changes preserved");
            for (int i = 0; i < 100; ++i) { glyph.Opacity(i % 2 ? 0.8 : 1); Check(Equal(glyph.Opacity(), 0.6), "shell opacity writes immediately reapplied"); }
            view.Refresh(false); Check(Equal(glyph.Opacity(), 0.8), "latest stock opacity restored on controller stop");
        }
        glyph.ClearValue(xaml::UIElement::OpacityProperty());
        { acrylic::ChevronView view(glyph); view.Refresh(true); }
        Check(glyph.ReadLocalValue(xaml::UIElement::OpacityProperty()) == unset, "unset opacity restored on destruction");
        xaml::Data::Binding binding; binding.Source(winrt::box_value(0.75));
        glyph.SetBinding(xaml::UIElement::OpacityProperty(), binding);
        { acrylic::ChevronView view(glyph); view.Refresh(true); view.Restore(); }
        const auto expression = glyph.GetBindingExpression(xaml::UIElement::OpacityProperty());
        Check(expression && expression.ParentBinding() == binding && Equal(glyph.Opacity(), 0.75), "stock opacity binding restored");
        glyph.Name(L"OtherTextBlock"); Check(!acrylic::FindChevronGlyph(root, Name), "unknown glyph name rejected");
        glyph.Name(L"InnerTextBlock"); content.Name(L"OtherGrid"); Check(!acrylic::FindChevronGlyph(root, Name), "unknown hierarchy rejected");
        content.Name(L"ContentGrid"); Alias(root, L"SystemTray.NotifyIconView");
        Check(!acrylic::FindChevronGlyph(root, Name), "neighboring tray icon rejected");
        Alias(root, L"SystemTray.ChevronIconView"); adaptive.Children().Clear();
        Check(!acrylic::FindChevronGlyph(root, Name), "missing template glyph left alone");
        controls::TextBlock replacement; replacement.Name(L"InnerTextBlock"); adaptive.Children().Append(replacement);
        Check(acrylic::FindChevronGlyph(root, Name) == replacement, "replacement template discovered");
        button.Click(click); source.Content(nullptr); names.clear();
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: native XAML chevron-only opacity, geometry/click/tooltip preservation, 100 shell writes, latest/unset/binding/destructor restoration, replacement and unsupported layout checks.");
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        Run(); manager.Close(); Pump(); return 0;
    } catch (const winrt::hresult_error& e) { std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(e.code())); return 1; }
    catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
}

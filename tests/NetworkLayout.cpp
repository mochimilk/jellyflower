// SPDX-License-Identifier: GPL-3.0-or-later
#include <unknwn.h>
#include "../src/network.hpp"
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.h>
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
#include <cmath>
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
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Network layout fixture",
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
        network.glyph.Name(L"InnerTextBlock");
        const auto found=acrylic::FindNetworkTarget(root,Name);
        Check(found && found.slot==network.inner && found.glyph==network.glyph,"exact network slot selected");
        Check(!acrylic::FindNetworkTarget(root),"ordinary app tree rejected");
        const auto width=stack.ActualWidth();
        const auto count=network.inner.Children().Size();
        const auto original=network.glyph.Text();
        const auto tooltip=winrt::box_value(L"Internet access");controls::ToolTipService::SetToolTip(button,tooltip);
        {
            acrylic::NetworkView view(found);view.Refresh(true);button.UpdateLayout();
            Check(view.Applied() && network.glyph.Opacity()==0,"connected glyph replaced without changing text");
            Check(network.glyph.Text()==original && stack.ActualWidth()==width,"native glyph text and slot width preserved");
            Check(network.inner.Children().Size()==count+1,"one artwork overlay attached");
            const auto art=network.inner.Children().GetAt(count).as<controls::Viewbox>();
            Check(art.Width()==16 && art.Height()==16 && !art.IsHitTestVisible(),"centered noninteractive 16 DIP vector");
            Check(art.HorizontalAlignment()==xaml::HorizontalAlignment::Center && art.VerticalAlignment()==xaml::VerticalAlignment::Center,"art centered in native slot");
            const auto path=art.Child().as<xaml::Shapes::Path>();
            auto white=xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::White());network.glyph.Foreground(white);Pump();
            Check(path.Fill()==white,"native light/dark foreground binding follows shell");
            auto black=xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Black());network.glyph.Foreground(black);Pump();
            Check(path.Fill()==black,"foreground switches back without replacing vector");
            for(wchar_t offline:{wchar_t(0xE774),wchar_t(0xF384),wchar_t(0xEB55),wchar_t(0xEB56),wchar_t(0xF473),wchar_t(0xE871),wchar_t(0xFFFF)}) {
                network.glyph.Text(winrt::hstring(&offline,1));
                Check(!view.Applied() && network.glyph.Opacity()==1 && network.inner.Children().Size()==count,"offline/warning/unknown shell glyph immediately restored");
                network.glyph.Text(original);Check(view.Applied(),"reconnect reapplies immediately");
            }
            for(wchar_t connected:{wchar_t(0xE701),wchar_t(0xE872),wchar_t(0xE873),wchar_t(0xE874),wchar_t(0xE839),wchar_t(0xE968),wchar_t(0xF385),wchar_t(0xF386)}) {
                network.glyph.Text(winrt::hstring(&connected,1));Check(view.Applied(),"Wi-Fi signal and Ethernet variants supported");
            }
            network.glyph.Opacity(.7);Check(network.glyph.Opacity()==0 && std::abs(art.Opacity()-.7)<.000001,"shell opacity changes preserved behind overlay");
            network.inner.Children().RemoveAt(count);view.Refresh(true);Check(network.inner.Children().Size()==count+1,"removed overlay recovered without duplicate");
            auto peer=xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);
            peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke).as<xaml::Automation::Provider::IInvokeProvider>().Invoke();Pump();
            Check(clicks==1 && controls::ToolTipService::GetToolTip(button)==tooltip,"stock click and tooltip preserved");
            Check(volume.inner.Visibility()==xaml::Visibility::Visible && battery.inner.Visibility()==xaml::Visibility::Visible,"neighbors untouched");
            view.Refresh(false);Check(!view.Applied() && std::abs(network.glyph.Opacity()-.7)<.000001 && network.inner.Children().Size()==count,"exit restores latest native opacity and removes overlay");
        }
        network.glyph.ClearValue(xaml::UIElement::OpacityProperty());
        controls::Border opacitySource;opacitySource.Opacity(.6);
        xaml::Data::Binding binding;binding.Source(opacitySource);binding.Path(xaml::PropertyPath(L"Opacity"));
        network.glyph.SetBinding(xaml::UIElement::OpacityProperty(),binding);
        {
            acrylic::NetworkView view(found);view.Refresh(true);
        }
        Check(network.glyph.GetBindingExpression(xaml::UIElement::OpacityProperty())!=nullptr,"opacity binding restored on destruction");
        opacitySource.Opacity(.8);Pump();Check(std::abs(network.glyph.Opacity()-.8)<.000001,"restored binding remains live");
        root.Name(L"NotificationCenterButton");Check(!acrylic::FindNetworkTarget(root,Name),"wrong root rejected");
        root.Name(L"ControlCenterButton");stack.Children().RemoveAt(0);stack.Children().InsertAt(1,network.presenter);
        Check(!acrylic::FindNetworkTarget(root,Name),"reordered unsupported slots rejected");
        button.Click(token); source.Content(nullptr); names.clear();
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: connected Wi-Fi/Ethernet vector; offline/warning/unknown restoration; theme binding, native size/click/tooltip, overlay recovery, opacity/binding/exit restoration, unsupported tree rejection.");
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        Run(); manager.Close(); Pump(); return 0;
    } catch (const winrt::hresult_error& e) { std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(e.code())); std::fwprintf(stderr, L"%s\n", e.message().c_str()); return 1; }
    catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
}

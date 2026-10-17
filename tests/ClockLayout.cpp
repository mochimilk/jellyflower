// SPDX-License-Identifier: GPL-3.0-or-later
#include <unknwn.h>
#include "../src/clock.hpp"
#include "../src/clock_text.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <cstdio>
#include <stdexcept>
#include <cmath>

namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
using acrylic::ClockView;
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
controls::TextBlock Line(const controls::StackPanel& panel) {
    for (const auto& child : panel.Children()) {
        auto block = child.try_as<controls::TextBlock>();
        if (block && block.Name() == L"AcrylicTaskbarClockLine") return block;
    }
    return nullptr;
}
void Run() {
    SYSTEMTIME sample{2026, 6, 1, 22, 9, 41, 0, 0};
    Check(acrylic::LocalizedClock(sample,true,L"en-US") == L"Mon Jun 22  9:41 am", "screenshot format with lowercase am");
    Check(acrylic::LocalizedClock(sample, false,L"en-US") == L"9:41 am", "date off");
    sample.wHour = 0; sample.wMinute = 5;
    Check(acrylic::LocalizedClock(sample,true,L"en-US") == L"Mon Jun 22  12:05 am", "midnight");
    sample.wHour = 12;
    Check(acrylic::LocalizedClock(sample,true,L"en-US") == L"Mon Jun 22  12:05 pm", "noon");
    sample.wHour = 23; sample.wMonth = 12; sample.wDay = 1; sample.wDayOfWeek = 0;
    Check(acrylic::LocalizedClock(sample,true,L"en-US") == L"Tue Dec 1  11:05 pm", "day/month/hour without leading zero");
    sample.wMonth = 0;
    Check(acrylic::LocalizedClock(sample,true,L"en-US").empty(), "invalid calendar input");
    Check(acrylic::LocalizedClock(sample, false,L"en-US") == L"11:05 pm", "time-only ignores unused calendar fields");
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Clock layout test",
        WS_POPUP, 0, 0, 400, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "host window");
    xaml::Hosting::DesktopWindowXamlSource source;
    auto native = source.as<IDesktopWindowXamlSourceNative>();
    winrt::check_hresult(native->AttachToWindow(host));
    HWND island = nullptr;
    winrt::check_hresult(native->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 400, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    {
        controls::StackPanel panel;
        controls::TextBlock date, time;
        date.Name(L"DateInnerTextBlock"); time.Name(L"TimeInnerTextBlock");
        date.Text(L"6/22/2026"); time.Text(L"9:41 AM");
        date.FontSize(12); time.FontSize(12);
        const xaml::Media::SolidColorBrush ink(winrt::Windows::UI::Color{255, 255, 255, 255});
        date.Foreground(ink);
        panel.Children().Append(time); panel.Children().Append(date);
        controls::Button button; button.Content(panel);
        controls::ToolTip tooltip; tooltip.Content(winrt::box_value(L"Monday, June 22, 2026"));
        controls::ToolTipService::SetToolTip(button, tooltip);
        xaml::Automation::AutomationProperties::SetName(button, L"stock clock accessible name");
        int clicks = 0;
        const auto click = button.Click([&](auto&&, auto&&) { ++clicks; });
        source.Content(button); Pump(); button.UpdateLayout();
        // A cold XAML host may not have completed its first layout yet. Await
        // that baseline before comparing the stock and single-line heights.
        const ULONGLONG layoutDeadline = GetTickCount64() + 2000;
        while (panel.ActualHeight() <= 0 && GetTickCount64() < layoutDeadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            Pump(); button.UpdateLayout();
        }
        Check(panel.ActualHeight() > 0, "initial stock clock layout completed");
        Check(acrylic::IsClockPanel(panel), "expected named children");
        controls::StackPanel unrelated;
        Check(!acrylic::IsClockPanel(unrelated), "reject unrelated panel");
        const auto originalText = date.Text(), originalTime = time.Text();
        const auto unset = xaml::DependencyProperty::UnsetValue();
        Check(panel.ReadLocalValue(xaml::FrameworkElement::VerticalAlignmentProperty()) == unset, "initial alignment unset");
        const auto oldHeight = panel.ActualHeight();
        {
            ClockView view(panel); view.Refresh(true); button.UpdateLayout();
            Check(view.Applied() && panel.Children().Size() == 3, "one added line");
            const auto line = Line(panel);
            Check(line && line.IsHitTestVisible(), "line retains routed pointer hit testing");
            Check(date.Visibility() == xaml::Visibility::Collapsed && time.Visibility() == xaml::Visibility::Collapsed, "stock lines collapsed");
            Check(date.Text() == originalText && time.Text() == originalTime, "stock strings intact");
            Check(line.TextWrapping() == xaml::TextWrapping::NoWrap, "single-line layout");
            Check(panel.ActualHeight() > 0 && panel.ActualHeight() < oldHeight, "no empty second row");
            Check(line.Foreground() == ink, "live stock text color");
            Check(controls::ToolTipService::GetToolTip(button) == tooltip, "same tooltip object");
            Check(xaml::Automation::AutomationProperties::GetName(button) == L"stock clock accessible name", "stock accessible name");
            auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);
            peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke).as<xaml::Automation::Provider::IInvokeProvider>().Invoke();
            Pump(); Check(clicks == 1, "original button click handler");
            for (int i = 0; i < 50; ++i) view.Refresh(true);
            Check(panel.Children().Size() == 3, "refresh never duplicates line");
            const double withDate = line.ActualWidth();
            view.Refresh(true, {false}); button.UpdateLayout();
            Check(std::wstring_view(line.Text()).find(L" ") == line.Text().size() - 3 && line.ActualWidth() < withDate, "date off shrinks native clock line");
            view.Refresh(true, acrylic::DecodeClockOptions(0x201)); button.UpdateLayout();
            Check(Line(panel) == line && panel.Children().Size() == 3 && line.ActualWidth() > withDate / 2,
                "retired font bits keep native system text and date");
            Check(acrylic::DecodeClockOptions(0x200).Packed() == 0 && acrylic::DecodeClockOptions(0x201).Packed() == 1,
                "old font preferences cannot reenable custom rendering");
            time.Text(L"21:42");
            const xaml::Media::SolidColorBrush nextInk(winrt::Windows::UI::Color{255, 0, 0, 0});
            date.Foreground(nextInk); panel.Spacing(7); view.Refresh(true);
            Check(line.Foreground() == nextInk && panel.Spacing() == 0, "theme and shell layout updates");
            view.Refresh(false); button.UpdateLayout();
            Check(!view.Applied() && panel.Children().Size() == 2, "restore removes custom line");
            Check(time.Text() == L"21:42", "restore latest stock text");
            Check(date.Visibility() == xaml::Visibility::Visible && time.Visibility() == xaml::Visibility::Visible, "restore stock visibility");
            Check(date.ReadLocalValue(xaml::UIElement::VisibilityProperty()) == unset, "restore unset visibility");
            Check(panel.ReadLocalValue(xaml::FrameworkElement::VerticalAlignmentProperty()) == unset, "restore unset alignment");
            Check(panel.Spacing() == 7, "restore latest shell spacing");
            Check(panel.ActualHeight() > oldHeight, "stock two-row layout restored");
            view.Refresh(true, {true});
        }
        Check(panel.Children().Size() == 2 && date.Visibility() == xaml::Visibility::Visible, "destructor restoration");
        xaml::Data::Binding binding; binding.Source(winrt::box_value(xaml::Visibility::Visible));
        date.SetBinding(xaml::UIElement::VisibilityProperty(), binding);
        Check(date.GetBindingExpression(xaml::UIElement::VisibilityProperty()) != nullptr, "test binding established");
        { ClockView view(panel); view.Refresh(true); view.Restore(); }
        const auto expression = date.GetBindingExpression(xaml::UIElement::VisibilityProperty());
        Check(expression && expression.ParentBinding() == binding, "original binding restored");
        button.Click(click); source.Content(nullptr);
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: native XAML single-line/date-off measurement, stock system font and click/tooltip identity, retired font preference migration, lowercase am/pm, theme updates, repeated refresh, latest-value/unset/binding restoration.");
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        Run(); manager.Close(); Pump(); return 0;
    } catch (const winrt::hresult_error& error) {
        std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(error.code()));
        std::fwprintf(stderr, L"%s\n", error.message().c_str()); return 1;
    } catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); return 1; }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <unknwn.h>
#include "../src/clock.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <cstdio>
#include <stdexcept>
#include <cmath>
#include "../src/clock_font.hpp"

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
controls::Image Image(const controls::StackPanel& panel) {
    for (const auto& child : panel.Children()) {
        const auto slot = child.try_as<controls::Canvas>();
        if (slot && slot.Name() == L"AcrylicTaskbarClockFontSlot" && slot.Children().Size())
            return slot.Children().GetAt(0).try_as<controls::Image>();
    }
    return nullptr;
}
void Run() {
    SYSTEMTIME sample{2026, 6, 1, 22, 9, 41, 0, 0};
    Check(acrylic::FormatClock(sample) == L"Mon Jun 22  9:41 am", "screenshot format with lowercase am");
    Check(acrylic::FormatClock(sample, false) == L"9:41 am", "date off");
    sample.wHour = 0; sample.wMinute = 5;
    Check(acrylic::FormatClock(sample) == L"Mon Jun 22  12:05 am", "midnight");
    sample.wHour = 12;
    Check(acrylic::FormatClock(sample) == L"Mon Jun 22  12:05 pm", "noon");
    sample.wHour = 23; sample.wMonth = 12; sample.wDay = 1; sample.wDayOfWeek = 0;
    Check(acrylic::FormatClock(sample) == L"Sun Dec 1  11:05 pm", "day/month/hour without leading zero");
    sample.wMonth = 0;
    Check(acrylic::FormatClock(sample).empty(), "invalid calendar input");
    Check(acrylic::FormatClock(sample, false) == L"11:05 pm", "time-only ignores unused calendar fields");
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
        controls::Image lastImage{nullptr};
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
            view.Refresh(true, {false, acrylic::ClockFont::System}); button.UpdateLayout();
            Check(std::wstring_view(line.Text()).find(L" ") == line.Text().size() - 3 && line.ActualWidth() < withDate, "date off shrinks native clock line");
            for (const auto choice : {acrylic::ClockFont::Playground, acrylic::ClockFont::Pangaia}) {
                view.Refresh(true, {true, choice}); button.UpdateLayout();
                const auto image = Image(panel);
                Check(image && image.IsHitTestVisible() && panel.Children().Size() == 3, "custom font keeps stock button ancestry and one added visual");
                const auto bitmap = image.Source().try_as<xaml::Media::Imaging::WriteableBitmap>();
                Check(SUCCEEDED(view.FontResult()) && bitmap && bitmap.PixelWidth() > 20 && bitmap.PixelHeight() > 5, "actual external font reaches XAML clock image");
                const double fullWidth = image.Width();
                const auto slot = image.Parent().as<controls::Canvas>();
                line.Measure({4096, 512});
                Check(std::abs(slot.Height() - line.DesiredSize().Height) < 0.01, "custom font uses native line height despite decorative strokes");
                acrylic::ClockFontRenderer geometry(acrylic::ClockFontPath(choice));
                const auto expected = geometry.Render(std::wstring(line.Text()), 12, 1, 0xFFFFFFFF, std::wstring(line.FontFamily().Source()), line.FontWeight().Weight);
                const double scale = panel.XamlRoot().RasterizationScale();
                const double opticalOffset = choice == acrylic::ClockFont::Playground ? 1.0 : 0;
                Check(std::abs(controls::Canvas::GetTop(image) + expected.baseline - line.BaselineOffset() - opticalOffset) <= 0.5 / scale + 0.01,
                    "font-specific optical baseline is preserved within nearest-pixel rounding");
                Check(std::abs(image.ActualWidth() * scale - bitmap.PixelWidth()) < 0.001
                    && std::abs(image.ActualHeight() * scale - bitmap.PixelHeight()) < 0.001,
                    "native XAML image maps exactly one source pixel to one screen pixel");
                const auto origin = image.TransformToVisual(nullptr).TransformPoint({0, 0});
                Check(std::abs(origin.X * scale - std::round(origin.X * scale)) < 0.001
                    && std::abs(origin.Y * scale - std::round(origin.Y * scale)) < 0.001,
                    "native XAML image starts on whole screen pixels");
                xaml::Media::TranslateTransform fractional; fractional.X(0.25); fractional.Y(0.25);
                button.RenderTransform(fractional); view.Refresh(true, {true, choice}); button.UpdateLayout();
                const auto moved = image.TransformToVisual(nullptr).TransformPoint({0, 0});
                Check(std::abs(moved.X * scale - std::round(moved.X * scale)) < 0.001
                    && std::abs(moved.Y * scale - std::round(moved.Y * scale)) < 0.001,
                    "fractional ancestor position does not resample the glyph image");
                button.RenderTransform(nullptr); view.Refresh(true, {true, choice}); button.UpdateLayout();
                for (int repeat = 0; repeat < 10; ++repeat) view.Refresh(true, {true, choice});
                Check(Image(panel) == image && SUCCEEDED(view.FontResult()), "repeated font refresh retains custom presentation");
                view.Refresh(true, {false, choice}); button.UpdateLayout();
                Check(image.Width() < fullWidth && panel.Children().Size() == 3, "custom font date off shrinks without duplicate visuals");
                Check(std::abs(slot.Height() - line.DesiredSize().Height) < 0.01, "date toggle preserves native baseline slot");
                Check(controls::ToolTipService::GetToolTip(button) == tooltip, "font switch preserves stock tooltip");
                peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke).as<xaml::Automation::Provider::IInvokeProvider>().Invoke();
                Pump();
            }
            Check(clicks == 3, "both custom fonts preserve stock button click handler");
            view.Refresh(true); button.UpdateLayout();
            Check(Line(panel) == line && !Image(panel) && panel.Children().Size() == 3, "segoe restores native text presentation");
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
            view.Refresh(true, {true, acrylic::ClockFont::Playground});
            lastImage = Image(panel);
        }
        Check(panel.Children().Size() == 2 && date.Visibility() == xaml::Visibility::Visible, "destructor restoration");
        Check(lastImage && !lastImage.Source(), "exit releases custom font bitmap");
        xaml::Data::Binding binding; binding.Source(winrt::box_value(xaml::Visibility::Visible));
        date.SetBinding(xaml::UIElement::VisibilityProperty(), binding);
        Check(date.GetBindingExpression(xaml::UIElement::VisibilityProperty()) != nullptr, "test binding established");
        { ClockView view(panel); view.Refresh(true); view.Restore(); }
        const auto expression = date.GetBindingExpression(xaml::UIElement::VisibilityProperty());
        Check(expression && expression.ParentBinding() == binding, "original binding restored");
        button.Click(click); source.Content(nullptr);
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: native XAML single-line/date-off measurement, both external font images, stock click/tooltip identity, font switching, lowercase am/pm, theme updates, repeated refresh, latest-value/unset/binding restoration.");
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

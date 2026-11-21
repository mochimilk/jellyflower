// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dock.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace shapes = xaml::Shapes;
using acrylic::DockBox;
std::unordered_map<void*, winrt::hstring> names;
winrt::hstring Name(const xaml::DependencyObject& object) {
    const auto found = names.find(winrt::get_abi(object));
    return found == names.end() ? winrt::get_class_name(object) : found->second;
}
void Alias(const xaml::DependencyObject& object, const wchar_t* value) { names[winrt::get_abi(object)] = value; }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
bool Near(double a, double b) { return std::fabs(a - b) < 0.01; }
bool Near(const DockBox& a, const DockBox& b) { return Near(a.left, b.left) && Near(a.top, b.top) && Near(a.right, b.right) && Near(a.bottom, b.bottom); }
bool Near(const xaml::Thickness& a, const xaml::Thickness& b) { return Near(a.Left, b.Left) && Near(a.Top, b.Top) && Near(a.Right, b.Right) && Near(a.Bottom, b.Bottom); }
bool Has(const std::vector<DockBox>& hits, const DockBox& box) { return std::any_of(hits.begin(), hits.end(), [&](const DockBox& hit) { return Near(hit, box); }); }
bool Unset(const xaml::DependencyObject& object, const xaml::DependencyProperty& key) { return object.ReadLocalValue(key) == xaml::DependencyProperty::UnsetValue(); }
controls::Button Item(const controls::Canvas& repeater, const wchar_t* type, double left, double width, double top = 0, double height = 48) {
    controls::Button button; Alias(button, type);
    button.Width(width); button.Height(height);
    controls::Canvas::SetLeft(button, left); controls::Canvas::SetTop(button, top);
    repeater.Children().Append(button);
    return button;
}
bool SameRgn(HWND window, const std::vector<RECT>& rects) {
    HRGN current = CreateRectRgn(0, 0, 0, 0), expected = CreateRectRgn(0, 0, 0, 0);
    for (const auto& rect : rects) { HRGN part = CreateRectRgnIndirect(&rect); CombineRgn(expected, expected, part, RGN_OR); DeleteObject(part); }
    const bool same = GetWindowRgn(window, current) != ERROR && EqualRgn(current, expected);
    DeleteObject(current); DeleteObject(expected);
    return same;
}
bool NoRgn(HWND window) {
    HRGN current = CreateRectRgn(0, 0, 0, 0);
    const bool none = GetWindowRgn(window, current) == ERROR;
    DeleteObject(current);
    return none;
}
LRESULT CALLBACK Plain(HWND window, UINT message, WPARAM wParam, LPARAM lParam) { return DefWindowProcW(window, message, wParam, lParam); }
void Windows() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    for (const wchar_t* name : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Windows.UI.Composition.DesktopWindowContentBridge"}) {
        WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = Plain; wc.lpszClassName = name;
        Check(RegisterClassW(&wc) != 0, "fixture window class");
    }
    auto bar = [&](const wchar_t* type, int width, int height, int top, DWORD ex = 0) {
        const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | ex, type, nullptr, WS_POPUP, 0, 0, width, height + top, nullptr, nullptr, instance, nullptr);
        CreateWindowExW(0, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr, WS_CHILD | WS_VISIBLE, 0, top, width, height, window, nullptr, instance, nullptr);
        return window;
    };
    const DWORD thread = GetCurrentThreadId();
    POINT offset{};
    HRESULT error = S_OK;
    Check(!acrylic::FindDockWindow({400, 72}, nullptr, thread, offset, error) && error == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "no taskbar window, no region");
    const HWND primary = bar(L"Shell_TrayWnd", 400, 72, 6);
    Check(acrylic::FindDockWindow({400, 72}, nullptr, thread, offset, error) == primary && offset.x == 0 && offset.y == 6 && error == S_OK, "island matched by pixel size, offset inside its window");
    Check(acrylic::FindDockWindow({401, 71}, nullptr, thread, offset, error) == primary, "a pixel of rounding still matches");
    Check(!acrylic::FindDockWindow({300, 72}, nullptr, thread, offset, error) && error == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "other sizes do not match");
    const HWND secondary = bar(L"Shell_SecondaryTrayWnd", 400, 72, 6);
    SetWindowPos(secondary, nullptr, 1000, 500, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Check(!acrylic::FindDockWindow({400, 72}, nullptr, thread, offset, error) && error == TYPE_E_AMBIGUOUSNAME, "two identical taskbars are ambiguous without a screen point");
    const POINT onSecondary{1200, 540}, onPrimary{200, 40}, nowhere{-500, -500};
    Check(acrylic::FindDockWindow({400, 72}, &onSecondary, thread, offset, error) == secondary && offset.y == 6, "a screen point picks the secondary taskbar");
    Check(acrylic::FindDockWindow({400, 72}, &onPrimary, thread, offset, error) == primary, "a screen point picks the primary taskbar");
    Check(!acrylic::FindDockWindow({400, 72}, &nowhere, thread, offset, error) && error == TYPE_E_AMBIGUOUSNAME, "a point on neither stays ambiguous and stock");
    DestroyWindow(secondary);
    const HWND mirrored = bar(L"Shell_SecondaryTrayWnd", 500, 48, 0, WS_EX_LAYOUTRTL);
    Check(!acrylic::FindDockWindow({500, 48}, nullptr, thread, offset, error) && error == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), "mirrored taskbars are left stock");
    DestroyWindow(mirrored);

    const std::vector<RECT> first{{10, 3, 150, 69}, {300, 0, 400, 72}}, second{{20, 3, 160, 69}, {300, 0, 400, 72}};
    {
        acrylic::DockRegion region;
        Check(SUCCEEDED(region.Apply(primary, first)) && region.Applied() && SameRgn(primary, first), "region applied");
        Check(SUCCEEDED(region.Apply(primary, first)) && SameRgn(primary, first), "same region is kept");
        Check(SUCCEEDED(region.Apply(primary, second)) && SameRgn(primary, second), "moved region follows the dock");
        SetWindowRgn(primary, nullptr, FALSE);
        Check(SUCCEEDED(region.Apply(primary, second)) && SameRgn(primary, second), "a region the shell dropped is set again");
        region.Restore();
        Check(!region.Applied() && NoRgn(primary), "restore leaves no region");
        Check(SUCCEEDED(region.Apply(primary, {})) && !region.Applied() && NoRgn(primary), "nothing to keep, nothing applied");

        // Windows 11's own stock region covers the whole taskbar window.
        const std::vector<RECT> whole{{0, 0, 400, 78}};
        SetWindowRgn(primary, CreateRectRgn(0, 0, 400, 78), FALSE);
        Check(SUCCEEDED(region.Apply(primary, first)) && region.Applied() && SameRgn(primary, first), "the shell's whole-window region is stock, not another program's");
        region.Restore();
        Check(!region.Applied() && SameRgn(primary, whole), "restore puts the whole-window region back");
        Check(SUCCEEDED(region.Apply(primary, first)), "applied over the stock region again");
        SetWindowRgn(primary, CreateRectRgn(0, 0, 400, 78), FALSE);
        Check(SUCCEEDED(region.Apply(primary, first)) && SameRgn(primary, first), "the shell writing its stock region again is followed by ours");
        SetWindowRgn(primary, nullptr, FALSE);
        Check(SUCCEEDED(region.Apply(primary, first)) && SameRgn(primary, first), "the shell clearing it is followed by ours too");
        region.Restore();
        Check(NoRgn(primary), "the latest stock state, no region, comes back");

        HRGN foreign = CreateRectRgn(0, 0, 50, 50);
        SetWindowRgn(primary, foreign, FALSE);
        Check(region.Apply(primary, first) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && !region.Applied() && SameRgn(primary, {{0, 0, 50, 50}}), "another program's region is never replaced");
        SetWindowRgn(primary, nullptr, FALSE);
        Check(SUCCEEDED(region.Apply(primary, first)), "applied again once the other region is gone");
        foreign = CreateRectRgn(0, 0, 60, 60);
        SetWindowRgn(primary, foreign, FALSE);
        Check(region.Apply(primary, second) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && !region.Applied(), "a region replaced by another program is given up");
        region.Restore();
        Check(SameRgn(primary, {{0, 0, 60, 60}}), "restore never removes another program's region");
        SetWindowRgn(primary, nullptr, FALSE);
        Check(SUCCEEDED(region.Apply(primary, first)), "applied for the destructor check");
    }
    Check(NoRgn(primary), "destruction restores the window");
    DestroyWindow(primary);
}
void Run() {
    Windows();
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Dock layout fixture",
        WS_POPUP, 0, 0, 800, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "host window");
    xaml::Hosting::DesktopWindowXamlSource source;
    auto native = source.as<IDesktopWindowXamlSourceNative>(); winrt::check_hresult(native->AttachToWindow(host));
    HWND island = nullptr; winrt::check_hresult(native->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 800, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    {
        // The island tree read from the live taskbar, with the shell's class names.
        controls::Grid content; content.Width(800); content.Height(48);
        controls::Grid frame; Alias(frame, L"Taskbar.TaskbarFrame"); frame.Name(L"TaskbarFrame");
        controls::Grid bar; bar.Name(L"RootGrid"); bar.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 255, 255, 255}));
        controls::Grid backgroundControl; Alias(backgroundControl, L"Taskbar.TaskbarBackground"); backgroundControl.IsHitTestVisible(false);
        controls::Grid backgroundGrid;
        shapes::Rectangle fill; fill.Name(L"BackgroundFill"); fill.Fill(media::SolidColorBrush(winrt::Windows::UI::Color{200, 32, 32, 32}));
        shapes::Rectangle stroke; stroke.Name(L"BackgroundStroke"); stroke.Height(1); stroke.VerticalAlignment(xaml::VerticalAlignment::Top);
        stroke.Fill(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        backgroundGrid.Children().Append(fill); backgroundGrid.Children().Append(stroke);
        backgroundControl.Children().Append(backgroundGrid);
        controls::Canvas repeater; repeater.Name(L"TaskbarFrameRepeater");
        const auto widgets = Item(repeater, L"Taskbar.AugmentedEntryPointButton", 0, 44);
        const auto start = Item(repeater, L"Taskbar.ExperienceToggleButton", 300, 45);
        Item(repeater, L"Taskbar.TaskListButton", 345, 44);
        Item(repeater, L"Taskbar.TaskListButton", 389, 44);
        const auto last = Item(repeater, L"Taskbar.TaskListButton", 433, 44);
        Item(repeater, L"Taskbar.TaskListButton", -10044, 44, -10032, 32); // recycled
        controls::Grid gripper; gripper.Name(L"GripperControl"); gripper.IsHitTestVisible(false); gripper.Width(120); gripper.Height(24);
        gripper.VerticalAlignment(xaml::VerticalAlignment::Top);
        shapes::Rectangle edge; edge.Name(L"ScreenEdgeStroke"); edge.IsHitTestVisible(false); edge.Height(2); edge.VerticalAlignment(xaml::VerticalAlignment::Bottom);
        for (const xaml::UIElement child : std::initializer_list<xaml::UIElement>{backgroundControl, repeater, gripper, edge}) bar.Children().Append(child);
        frame.Children().Append(bar);
        controls::Grid tray; Alias(tray, L"SystemTray.SystemTrayFrame"); tray.Width(60); tray.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        tray.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 255, 255, 255}));
        content.Children().Append(frame); content.Children().Append(tray);
        source.Content(content); Pump(); content.UpdateLayout();

        const auto margin = xaml::FrameworkElement::MarginProperty();
        const auto radiusX = shapes::Rectangle::RadiusXProperty();
        const auto height = xaml::FrameworkElement::HeightProperty();
        const auto vertical = xaml::FrameworkElement::VerticalAlignmentProperty();
        const auto strokeKey = shapes::Shape::StrokeProperty();
        // A stock binding on the stroke height, as the shell might use.
        xaml::Data::Binding binding; binding.Source(winrt::box_value(1.0));
        stroke.SetBinding(height, binding);
        const media::SolidColorBrush outline(winrt::Windows::UI::Color{40, 255, 255, 255});
        unsigned moved = 0;
        {
            acrylic::DockView view(frame, Name);
            view.OnMoved([&] { ++moved; });
            Check(!view.Applied() && view.Hits().empty(), "nothing changes before the first refresh");
            view.Refresh(false, outline);
            Check(!view.Applied() && Unset(fill, margin) && Near(stroke.ActualHeight(), 1), "off leaves the stock bar alone");

            view.Refresh(true, outline); content.UpdateLayout(); Pump();
            Check(view.Applied() && view.Error() == S_OK, "dock applied");
            const xaml::Thickness pillMargin{288, 3, 800 - 489, 3};
            Check(Near(fill.Margin(), pillMargin) && Near(fill.RadiusX(), 15.75) && Near(fill.RadiusY(), 15.75), "fill reshaped into the pill around Start and the apps");
            Check(Near(fill.ActualWidth(), 201) && Near(fill.ActualHeight(), 42), "pill laid out at its size");
            Check(Near(stroke.Margin(), pillMargin) && stroke.VerticalAlignment() == xaml::VerticalAlignment::Stretch && std::isnan(stroke.Height())
                && Near(stroke.ActualHeight(), 42) && stroke.Stroke() == outline && Near(stroke.RadiusX(), 15.75), "stroke becomes the pill's outline in the supplied brush");
            Check(fill.Fill().as<media::SolidColorBrush>().Color().A == 200 && stroke.Fill().as<media::SolidColorBrush>().Color().A == 0, "fills are not touched");
            const auto& hits = view.Hits();
            Check(!hits.empty() && Near(hits[0], {288, 3, 489, 45}), "first hit is the pill, in root coordinates");
            Check(Has(hits, {0, 0, 44, 48}), "widgets outside the pill stay clickable");
            Check(Has(hits, {340, 0, 460, 24}), "tablet gripper stays in the region");
            Check(Has(hits, {740, 0, 800, 48}), "notification area beside the frame stays clickable");
            Check(hits.size() == 4, "recycled buttons, the edge stroke and the background are not hits");

            // An app opens: the pill follows in the same layout pass.
            const auto added = Item(repeater, L"Taskbar.TaskListButton", 477, 44);
            content.UpdateLayout(); Pump();
            Check(moved > 0 && Near(fill.Margin(), {288, 3, 800 - 533, 3}) && Near(view.Hits()[0], {288, 3, 533, 45}), "pill and hits follow a new button");
            added.Visibility(xaml::Visibility::Collapsed);
            content.UpdateLayout(); Pump();
            Check(Near(fill.Margin(), pillMargin), "pill shrinks back when a button goes");
            moved = 0;
            content.UpdateLayout(); Pump();
            Check(moved == 0, "an unchanged layout reports nothing");

            // The shell writes a value while the dock is on.
            fill.Margin({1, 1, 1, 1});
            Check(Near(fill.Margin(), pillMargin), "shell writes re-shaped immediately");
            for (int i = 0; i < 20; ++i) view.Refresh(true, outline);
            Check(Near(fill.Margin(), pillMargin) && stroke.Stroke() == outline, "repeated refresh is stable");
            const media::SolidColorBrush lightOutline(winrt::Windows::UI::Color{40, 0, 0, 0});
            view.Refresh(true, lightOutline);
            Check(stroke.Stroke() == lightOutline, "theme change brings the new stock stroke brush");

            // Everything hidden (a collapsed tablet taskbar): stock bar, nothing to keep.
            for (const auto& child : repeater.Children()) child.Visibility(xaml::Visibility::Collapsed);
            content.UpdateLayout(); Pump();
            Check(!view.Applied() && view.Hits().empty() && Near(fill.Margin(), {1, 1, 1, 1}) && Near(stroke.ActualHeight(), 1), "no group gives the stock bar back");
            for (const auto& child : repeater.Children()) child.Visibility(xaml::Visibility::Visible);
            added.Visibility(xaml::Visibility::Collapsed);
            content.UpdateLayout(); Pump();
            Check(view.Applied() && Near(fill.Margin(), pillMargin), "the dock returns with the buttons");

            // The taller bar. A button laid out like the shell's: a panel named
            // IconPanel whose padding insets the hover/running highlight.
            controls::Grid button; Alias(button, L"Taskbar.TaskListButton"); button.Width(44); button.Height(64);
            controls::Grid iconPanel; iconPanel.Name(L"IconPanel"); iconPanel.Padding({2, 4, 2, 4});
            controls::Border highlight; highlight.Name(L"BackgroundElement"); highlight.CornerRadius({4, 4, 4, 4});
            iconPanel.Children().Append(highlight);
            button.Children().Append(iconPanel); controls::Canvas::SetLeft(button, 477); repeater.Children().Append(button);
            const auto corner = [&](double radius) { const auto c = highlight.CornerRadius(); return Near(c.TopLeft, radius) && Near(c.TopRight, radius) && Near(c.BottomRight, radius) && Near(c.BottomLeft, radius); };
            content.UpdateLayout(); Pump();
            const double scale = content.XamlRoot().RasterizationScale();
            const double tall = MulDiv(64, static_cast<int>(std::lround(scale * 96)), 96) / scale;
            view.Refresh(true, outline, 64); content.UpdateLayout(); Pump();
            Check(view.Height() == 64 && Near(frame.Height(), tall) && Near(tray.Height(), tall), "frame and notification area held at the taller bar, in whole pixels");
            Check(Near(iconPanel.Padding(), {2, 12, 2, 12}), "the highlight stays a 40x40 square in the middle of the taller button");
            Check(corner(9), "the highlight takes an app icon's corners: 22.5% of 40 DIPs");
            highlight.CornerRadius({4, 4, 4, 4});
            Check(corner(9), "the shell writing its stock corners is answered");
            frame.Height(48);
            Check(Near(frame.Height(), tall), "the shell writing the stock height is answered");
            for (const auto& child : repeater.Children()) child.Visibility(xaml::Visibility::Collapsed);
            content.UpdateLayout(); Pump();
            Check(!view.Applied() && view.Height() == 64 && Near(frame.Height(), tall), "an empty group keeps the bar's height");
            for (const auto& child : repeater.Children()) child.Visibility(xaml::Visibility::Visible);
            added.Visibility(xaml::Visibility::Collapsed);
            view.Refresh(true, outline, 0); content.UpdateLayout(); Pump();
            Check(view.Height() == 0 && view.Applied() && Near(frame.Height(), 48) && Unset(tray, height) && Near(iconPanel.Padding(), {2, 4, 2, 4}),
                "back to the stock height: the shell's latest frame height, unset tray height, stock padding");
            Check(corner(9), "app-icon corners stay while the dock is on, whatever the bar's height");
            view.Refresh(false, outline); content.UpdateLayout(); Pump();
            Check(corner(4), "turning the dock off gives the highlight its stock corners back");
            view.Refresh(true, outline); content.UpdateLayout(); Pump();
            repeater.Children().RemoveAt(repeater.Children().Size() - 1);
            frame.ClearValue(height);

            view.Refresh(false, outline); content.UpdateLayout(); Pump();
            Check(!view.Applied() && view.Hits().empty(), "turning off releases the dock");
            Check(Near(fill.Margin(), {1, 1, 1, 1}) && Unset(fill, radiusX), "latest shell margin and unset radius restored");
            const auto expression = stroke.GetBindingExpression(height);
            Check(expression && expression.ParentBinding() == binding && Near(stroke.ActualHeight(), 1), "stock height binding restored");
            Check(stroke.VerticalAlignment() == xaml::VerticalAlignment::Top && Unset(stroke, strokeKey), "stock alignment and no stroke restored");
            moved = 0;
            start.Width(60); content.UpdateLayout(); Pump();
            Check(moved == 0 && Near(fill.Margin(), {1, 1, 1, 1}), "off stops following the layout");
            start.Width(45);
            view.Refresh(true, outline); content.UpdateLayout();
            Check(view.Applied(), "on again");
            (void)widgets; (void)last; (void)vertical;
        }
        content.UpdateLayout();
        Check(Near(fill.Margin(), {1, 1, 1, 1}) && Unset(fill, radiusX) && Near(stroke.ActualHeight(), 1) && Unset(stroke, strokeKey), "destruction restores the stock bar");
        {
            // A frame without the shell's parts is left alone.
            controls::Grid empty; Alias(empty, L"Taskbar.TaskbarFrame");
            acrylic::DockView view(empty, Name);
            view.Refresh(true, outline);
            Check(!view.Applied() && view.Error() == S_OK, "unknown layouts stay stock");
        }
        source.Content(nullptr); names.clear();
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: floating dock pill around the app group (recycled buttons ignored), outline in the stock stroke brush, layout following, shell writes, empty-group and unknown-layout fallback, latest/unset/binding/destructor restoration; region rects, window matching (identical monitors by screen point, ambiguous, mirrored), and region ownership (never replaces or removes another program's region).");
}
int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    try { Run(); return 0; }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); }
    catch (const winrt::hresult_error& error) { std::printf("FAIL: HRESULT 0x%08x %ls\n", static_cast<unsigned>(error.code()), error.message().c_str()); }
    return 1;
}

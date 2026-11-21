// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/tray_only.hpp"
#include "../src/localization.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <unordered_map>
#include <cstdio>
#include <stdexcept>
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
using xaml::Visibility;
std::unordered_map<void*, winrt::hstring> names;
winrt::hstring Name(const xaml::DependencyObject& object) {
    const auto found = names.find(winrt::get_abi(object));
    return found == names.end() ? winrt::get_class_name(object) : found->second;
}
void Alias(const xaml::DependencyObject& object, const wchar_t* value) { names[winrt::get_abi(object)] = value; }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
controls::Button Icon(const wchar_t* tip) {
    controls::Button icon; icon.Name(L"NotifyItemIcon"); Alias(icon, L"SystemTray.NotifyIconView");
    icon.Width(32); icon.Height(40);
    if (tip) xaml::Automation::AutomationProperties::SetName(icon, tip);
    return icon;
}
template <typename T> T Part(const wchar_t* type, const wchar_t* name) {
    T part; part.Name(name); Alias(part, type); return part;
}
controls::Border Slot(const xaml::UIElement& child) { controls::Border slot; slot.Child(child); return slot; }
bool Hidden(const xaml::UIElement& element) { return element.Visibility() == Visibility::Collapsed; }
void Run() {
    const HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Tray-only layout fixture",
        WS_POPUP, 0, 0, 800, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "host window");
    xaml::Hosting::DesktopWindowXamlSource source;
    auto native = source.as<IDesktopWindowXamlSourceNative>(); winrt::check_hresult(native->AttachToWindow(host));
    HWND island = nullptr; winrt::check_hresult(native->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 800, 48, SWP_NOZORDER | SWP_NOACTIVATE);
    {
        controls::StackPanel taskbar; taskbar.Orientation(controls::Orientation::Horizontal);
        controls::Button app; app.Width(40); taskbar.Children().Append(app);
        auto frame = Part<controls::Grid>(L"SystemTray.SystemTrayFrame", L"");
        controls::StackPanel grid; grid.Name(L"SystemTrayFrameGrid"); grid.Orientation(controls::Orientation::Horizontal);
        auto notifyStack = Part<controls::StackPanel>(L"SystemTray.Stack", L"NotifyIconStack");
        controls::Button chevron; chevron.Width(24); Alias(chevron, L"SystemTray.ChevronIconView"); notifyStack.Children().Append(Slot(chevron));
        controls::StackPanel icons; icons.Name(L"NotificationAreaIcons"); icons.Orientation(controls::Orientation::Horizontal);
        auto flower = Icon(Tr(acrylic::TextId::TrayTip, acrylic::Language::English));
        auto update = Icon(L"Get the latest features and security improvements.");
        icons.Children().Append(Slot(update)); icons.Children().Append(Slot(flower));
        auto mainStack = Part<controls::StackPanel>(L"SystemTray.Stack", L"MainStack");
        controls::Button microphone; microphone.Width(24); mainStack.Children().Append(microphone);
        auto language = Part<controls::StackPanel>(L"SystemTray.Stack", L"NonActivatableStack");
        controls::TextBlock eng; eng.Text(L"ENG"); language.Children().Append(eng);
        auto control = Part<controls::Button>(L"SystemTray.OmniButton", L"ControlCenterButton"); control.Width(72);
        auto clock = Part<controls::Button>(L"SystemTray.OmniButton", L"NotificationCenterButton"); clock.Width(120);
        controls::TextBlock time; time.Text(L"9:41 am"); clock.Content(time);
        auto desktop = Part<controls::StackPanel>(L"SystemTray.Stack", L"ShowDesktopStack");
        controls::Button showDesktop; showDesktop.Width(12); desktop.Children().Append(showDesktop);
        auto unknown = Part<controls::StackPanel>(L"SystemTray.Stack", L"FutureStack");
        controls::Button future; future.Width(24); unknown.Children().Append(future);
        auto other = Part<controls::Button>(L"SystemTray.OmniButton", L"FutureButton"); other.Width(24);
        for (const xaml::UIElement child : std::initializer_list<xaml::UIElement>{notifyStack, icons, mainStack, language, control, clock, unknown, other, desktop})
            grid.Children().Append(child);
        frame.Children().Append(grid); taskbar.Children().Append(frame);
        source.Content(taskbar); Pump(); taskbar.UpdateLayout();
        const auto unset = xaml::DependencyProperty::UnsetValue();
        const auto property = xaml::UIElement::VisibilityProperty();

        Check(acrylic::FindTrayFrame(flower, Name) == frame && acrylic::FindTrayFrame(time, Name) == frame, "icons and clock find their notification area");
        Check(!acrylic::FindTrayFrame(app, Name), "taskbar app buttons are outside the notification area");
        Check(!acrylic::FindTrayFrame(flower), "ordinary controls rejected without shell metadata");
        // Kept alive for the whole fixture: aliases are keyed by object address.
        auto overflow = Part<controls::Grid>(L"SystemTray.NotificationAreaOverflow", L"");
        auto overflowIcon = Icon(L"overflow icon"); overflow.Children().Append(overflowIcon);
        Check(!acrylic::FindTrayFrame(overflowIcon, Name), "overflow flyout is never treated as the taskbar tray");
        for (unsigned i = 0; i < static_cast<unsigned>(acrylic::Language::Count); ++i)
            Check(acrylic::IsJellyflowerTip(Tr(acrylic::TextId::TrayTip, static_cast<acrylic::Language>(i))), "tray tip matches in every display language");
        Check(!acrylic::IsJellyflowerTip(L"") && !acrylic::IsJellyflowerTip(L"ur cute") && !acrylic::IsJellyflowerTip(L"Windows Update"), "other names rejected");

        // Stock states the shell might use: a binding, a local value and an unset value.
        xaml::Data::Binding binding; binding.Source(winrt::box_value(Visibility::Visible));
        language.SetBinding(property, binding);
        notifyStack.Visibility(Visibility::Visible);
        taskbar.UpdateLayout();
        const auto fullWidth = frame.ActualWidth();
        {
            acrylic::TrayOnlyView view(frame, Name);
            Check(view.Hidden() == 0 && !view.Found(), "nothing changes before the first refresh");
            view.Refresh(false);
            Check(view.Hidden() == 0 && !Hidden(clock), "off leaves the stock tray alone");
            view.Refresh(true); taskbar.UpdateLayout();
            Check(view.Found() && view.Error() == S_OK, "jellyflower's icon found by its accessible name");
            Check(view.Hidden() == 6, "chevron stack, other icon, status, language, quick settings and clock hidden");
            Check(Hidden(notifyStack) && Hidden(update) && Hidden(mainStack) && Hidden(language) && Hidden(control) && Hidden(clock), "known parts collapsed");
            Check(!Hidden(flower) && flower.ActualWidth() > 0, "jellyflower's icon stays visible");
            Check(!Hidden(desktop) && !Hidden(unknown) && !Hidden(other) && !Hidden(app), "show desktop, unknown parts and app buttons untouched");
            Check(frame.ActualWidth() < fullWidth, "hidden parts give back their taskbar space");
            Check(flower.ReadLocalValue(property) == unset && xaml::Automation::AutomationProperties::GetName(flower) == Tr(acrylic::TextId::TrayTip, acrylic::Language::English), "jellyflower's icon is not modified");

            for (int i = 0; i < 50; ++i) { update.Visibility(Visibility::Visible); Check(Hidden(update), "shell visibility writes immediately re-hidden"); }
            update.Visibility(Visibility::Visible); view.Refresh(true);
            Check(view.Hidden() == 6, "repeated refresh is stable");

            // Our icon moved to the overflow: the chevron must come back.
            xaml::Automation::AutomationProperties::SetName(flower, L"something else");
            view.Refresh(true); taskbar.UpdateLayout();
            Check(!view.Found() && !Hidden(notifyStack) && Hidden(flower) && view.Hidden() == 6, "without jellyflower on the taskbar the chevron stays reachable");
            // The compact status bar opens the menu, so nothing has to stay.
            view.Refresh(true, true); taskbar.UpdateLayout();
            Check(!view.Found() && Hidden(notifyStack) && view.Hidden() == 7, "with the menu in the status bar the chevron is hidden too");
            view.Refresh(true); taskbar.UpdateLayout();
            Check(!Hidden(notifyStack) && view.Hidden() == 6, "leaving the status bar brings the chevron back");
            xaml::Automation::AutomationProperties::SetName(flower, Tr(acrylic::TextId::TrayTip, acrylic::Language::Korean));
            view.Refresh(true);
            Check(view.Found() && Hidden(notifyStack) && !Hidden(flower), "jellyflower's icon in another display language restores the minimal tray");

            // Icons added and removed while active.
            auto added = Icon(L"New app"); icons.Children().Append(Slot(added));
            view.Refresh(true);
            Check(Hidden(added) && view.Hidden() == 7, "new notification icons are hidden");
            icons.Children().RemoveAt(icons.Children().Size() - 1);
            view.Refresh(true);
            Check(!Hidden(added) && view.Hidden() == 6, "removed icons are released with their stock visibility");

            // Name supplied by the icon's automation peer instead of the attached property.
            xaml::Automation::AutomationProperties::SetName(flower, L"");
            flower.Content(winrt::box_value(Tr(acrylic::TextId::TrayTip, acrylic::Language::French)));
            view.Refresh(true);
            Check(view.Found() && !Hidden(flower), "automation peer name identifies jellyflower's icon");

            view.Refresh(false); taskbar.UpdateLayout();
            Check(view.Hidden() == 0 && !view.Found(), "turning off releases every part");
            Check(!Hidden(update) && update.ReadLocalValue(property) != unset, "latest shell value restored");
            Check(mainStack.ReadLocalValue(property) == unset && control.ReadLocalValue(property) == unset && clock.ReadLocalValue(property) == unset, "unset visibility restored");
            const auto expression = language.GetBindingExpression(property);
            Check(expression && expression.ParentBinding() == binding && !Hidden(language), "stock visibility binding restored");
            Check(!Hidden(notifyStack) && frame.ActualWidth() == fullWidth, "stock tray layout returns");

            // A part the shell had already collapsed stays collapsed after restore.
            notifyStack.Visibility(Visibility::Collapsed);
            view.Refresh(true); view.Refresh(false);
            Check(Hidden(notifyStack), "stock collapsed chevron stack stays collapsed");
            notifyStack.Visibility(Visibility::Visible);
            view.Refresh(true);
        }
        Check(!Hidden(notifyStack) && !Hidden(clock) && !Hidden(update) && !Hidden(control) && !Hidden(flower), "destruction restores the stock tray");
        source.Content(nullptr); names.clear();
    }
    source.Close(); DestroyWindow(host); Pump();
    std::puts("PASS: native XAML tray-only hiding keeps jellyflower's icon, show desktop and unknown parts; chevron kept when jellyflower is off the taskbar; shell writes re-hidden; new/removed icons; peer-name fallback; latest/unset/binding/collapsed/destructor restoration.");
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        Run(); manager.Close(); Pump(); return 0;
    } catch (const winrt::hresult_error& e) { std::printf("FAIL: XAML HRESULT %08lx\n", static_cast<unsigned long>(e.code())); return 1; }
    catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
}

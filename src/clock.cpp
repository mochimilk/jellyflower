// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock.hpp"
#include "adaptive_material.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <array>
#include <cwchar>

namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace {
controls::TextBlock FindText(const controls::StackPanel& panel, const wchar_t* name) {
    for (const auto& child : panel.Children()) {
        const auto text = child.try_as<controls::TextBlock>();
        if (text && text.Name() == name) return text;
    }
    return nullptr;
}
// Save bindings as bindings, and unset properties as unset. Restoring just an
// effective value would permanently override a theme/style or shell binding.
struct Property {
    xaml::FrameworkElement element{nullptr};
    xaml::DependencyProperty property{nullptr};
    winrt::Windows::Foundation::IInspectable local{nullptr};
    xaml::Data::BindingBase binding{nullptr};
    bool saved = false;
    Property(xaml::FrameworkElement target, xaml::DependencyProperty key)
        : element(std::move(target)), property(std::move(key)) {}
    void Save() {
        local = element.ReadLocalValue(property);
        const auto expression = element.GetBindingExpression(property);
        binding = expression ? expression.ParentBinding() : nullptr;
        saved = true;
    }
    void Restore() {
        if (!saved) return;
        element.ClearValue(property);
        if (binding) element.SetBinding(property, binding);
        else if (local != xaml::DependencyProperty::UnsetValue()) element.SetValue(property, local);
        saved = false;
    }
};
}
std::wstring FormatClock(const SYSTEMTIME& time, bool date) {
    constexpr const wchar_t* days[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    constexpr const wchar_t* months[]{L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
        L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    if (date && (time.wDayOfWeek > 6 || time.wMonth < 1 || time.wMonth > 12)) return {};
    wchar_t value[64]{};
    const unsigned hour = time.wHour % 12 ? time.wHour % 12 : 12;
    if (date) swprintf_s(value, L"%s %s %u  %u:%02u %s", days[time.wDayOfWeek], months[time.wMonth - 1],
        static_cast<unsigned>(time.wDay), hour, static_cast<unsigned>(time.wMinute), time.wHour < 12 ? L"am" : L"pm");
    else swprintf_s(value, L"%u:%02u %s", hour, static_cast<unsigned>(time.wMinute), time.wHour < 12 ? L"am" : L"pm");
    return value;
}
bool IsClockPanel(const controls::StackPanel& panel) {
    return panel && FindText(panel, L"DateInnerTextBlock") && FindText(panel, L"TimeInnerTextBlock");
}
struct ClockView::Impl {
    controls::StackPanel panel;
    controls::TextBlock date, time, line;
    std::array<Property, 4> properties;
    std::array<int64_t, 4> tokens{};
    size_t registered = 0;
    bool applied = false, updating = false;
    xaml::Media::Brush adaptiveInk{nullptr};
    explicit Impl(const controls::StackPanel& value)
        : panel(value), date(FindText(value, L"DateInnerTextBlock")), time(FindText(value, L"TimeInnerTextBlock")),
          properties{{{date, xaml::UIElement::VisibilityProperty()}, {time, xaml::UIElement::VisibilityProperty()},
              {panel, xaml::FrameworkElement::VerticalAlignmentProperty()}, {panel, controls::StackPanel::SpacingProperty()}}} {
        if (!date || !time) winrt::throw_hresult(E_INVALIDARG);
        line.Name(L"AcrylicTaskbarClockLine");
        // Keep pointer hits in the stock clock's routed-event ancestry. There
        // are no input handlers on this line to consume clicks or hover.
        line.IsHitTestVisible(true);
        line.TextWrapping(xaml::TextWrapping::NoWrap);
        line.TextTrimming(xaml::TextTrimming::None);
        line.TextAlignment(xaml::TextAlignment::Center);
        line.FlowDirection(xaml::FlowDirection::LeftToRight);
        xaml::Automation::AutomationProperties::SetAccessibilityView(line, xaml::Automation::Peers::AccessibilityView::Raw);
        try {
            for (size_t i = 0; i < properties.size(); ++i) {
                auto& p = properties[i];
                tokens[i] = p.element.RegisterPropertyChangedCallback(p.property,
                    [this, i](const xaml::DependencyObject&, const xaml::DependencyProperty&) { Changed(i); });
                ++registered;
            }
        } catch (...) { Unregister(); throw; }
    }
    void Unregister() noexcept {
        while (registered) {
            const size_t i = --registered;
            try { properties[i].element.UnregisterPropertyChangedCallback(properties[i].property, tokens[i]); }
            catch (...) {}
        }
    }
    void Override(size_t i) {
        if (i < 2) properties[i].element.Visibility(xaml::Visibility::Collapsed);
        else if (i == 2) panel.VerticalAlignment(xaml::VerticalAlignment::Center);
        else panel.Spacing(0);
    }
    bool IsOverride(size_t i) const {
        if (i < 2) return properties[i].element.Visibility() == xaml::Visibility::Collapsed;
        if (i == 2) return panel.VerticalAlignment() == xaml::VerticalAlignment::Center;
        return panel.Spacing() == 0;
    }
    void Changed(size_t i) noexcept {
        if (!applied || updating) return;
        try {
            if (IsOverride(i)) return;
            // A shell/theme write becomes the latest stock value for restoration.
            properties[i].Save();
            updating = true;
            Override(i);
            updating = false;
        } catch (...) { updating = false; }
    }
    void Restore() {
        if (!applied) return;
        updating = true;
        try {
            uint32_t index = 0;
            if (panel.Children().IndexOf(line, index)) panel.Children().RemoveAt(index);
            for (size_t i = 0; i < properties.size(); ++i) {
                if (IsOverride(i)) properties[i].Restore();
            }
            applied = false;
            updating = false;
        } catch (...) { updating = false; throw; }
    }
    void Refresh(bool enabled, ClockOptions options, bool contrast) {
        if (!enabled) { Restore(); return; }
        if (!applied) {
            for (auto& p : properties) p.Save();
            applied = true; // Rollback includes a partially applied layout.
            updating = true;
            try {
                for (size_t i = 0; i < properties.size(); ++i) Override(i);
                panel.Children().Append(line);
                updating = false;
            } catch (...) { updating = false; Restore(); throw; }
        }
        SYSTEMTIME now{};
        GetLocalTime(&now); // Picks up clock/time-zone/DST changes without registry writes.
        const auto text = FormatClock(now, options.date);
        if (line.Text() != text) line.Text(text);
        // Read the live stock appearance, including theme/high-contrast updates.
        line.FontFamily(date.FontFamily());
        line.FontSize(date.FontSize());
        line.FontWeight(date.FontWeight());
        line.FontStyle(date.FontStyle());
        const auto native=date.Foreground();
        const auto solid=native.try_as<xaml::Media::SolidColorBrush>();
        if(contrast && solid){
            const auto color=solid.Color();
            if(std::max({color.R,color.G,color.B})-std::min({color.R,color.G,color.B})<=2){
                try{if(!adaptiveInk)adaptiveInk=winrt::make<adaptive::Ink>(line,color);
                    adaptiveInk.Opacity(solid.Opacity()*color.A/255.);
                    adaptiveInk.as<xaml::Media::XamlCompositionBrushBase>().FallbackColor({255,color.R,color.G,color.B});
                }catch(...){adaptiveInk=nullptr;}
            }else adaptiveInk=nullptr;
        }else adaptiveInk=nullptr;
        const auto foreground=adaptiveInk?adaptiveInk:native;if(line.Foreground()!=foreground)line.Foreground(foreground);
        line.IsTextScaleFactorEnabled(date.IsTextScaleFactorEnabled());
    }
    ~Impl() { Unregister(); try { Restore(); } catch (...) {} }
};
ClockView::ClockView(const controls::StackPanel& panel) : impl(std::make_unique<Impl>(panel)) {}
ClockView::~ClockView() = default;
void ClockView::Refresh(bool enabled, ClockOptions options, bool contrast) { impl->Refresh(enabled, options, contrast); }
void ClockView::Restore() { impl->Restore(); }
bool ClockView::Applied() const noexcept { return impl->applied; }
}

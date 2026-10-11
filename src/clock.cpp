// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock.hpp"
#include "clock_font.hpp"
#include <robuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Storage.Streams.h>
#include <array>
#include <cwchar>
#include <cstring>
#include <cmath>

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
    controls::Image image;
    controls::Canvas fontCanvas;
    std::unique_ptr<ClockFontRenderer> renderer;
    ClockFont renderedFont = ClockFont::System;
    std::wstring renderedText;
    double renderedSize = 0, renderedScale = 0;
    uint32_t renderedColor = 0;
    std::wstring renderedFamily;
    UINT renderedWeight = 0;
    double bitmapBaseline = 0;
    double imageTop = 0;
    winrt::event_token layoutToken{};
    bool layoutRegistered = false, aligning = false;
    HRESULT fontResult = S_OK;
    std::array<Property, 4> properties;
    std::array<int64_t, 4> tokens{};
    size_t registered = 0;
    bool applied = false, updating = false;
    explicit Impl(const controls::StackPanel& value)
        : panel(value), date(FindText(value, L"DateInnerTextBlock")), time(FindText(value, L"TimeInnerTextBlock")),
          properties{{{date, xaml::UIElement::VisibilityProperty()}, {time, xaml::UIElement::VisibilityProperty()},
              {panel, xaml::FrameworkElement::VerticalAlignmentProperty()}, {panel, controls::StackPanel::SpacingProperty()}}} {
        if (!date || !time) winrt::throw_hresult(E_INVALIDARG);
        line.Name(L"AcrylicTaskbarClockLine");
        image.Name(L"AcrylicTaskbarClockFont");
        fontCanvas.Name(L"AcrylicTaskbarClockFontSlot");
        fontCanvas.Children().Append(image);
        xaml::Automation::AutomationProperties::SetAccessibilityView(fontCanvas, xaml::Automation::Peers::AccessibilityView::Raw);
        image.IsHitTestVisible(true);
        image.Stretch(xaml::Media::Stretch::Fill);
        // We align the final screen position ourselves. A second local rounding
        // pass can undo that correction when an ancestor starts between pixels.
        image.UseLayoutRounding(false);
        xaml::Automation::AutomationProperties::SetAccessibilityView(image, xaml::Automation::Peers::AccessibilityView::Raw);
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
            layoutToken = fontCanvas.LayoutUpdated([this](auto&&, auto&&) { AlignImage(); });
            layoutRegistered = true;
        } catch (...) { Unregister(); throw; }
    }
    void Unregister() noexcept {
        if (layoutRegistered) {
            try { fontCanvas.LayoutUpdated(layoutToken); } catch (...) {}
            layoutRegistered = false;
        }
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
            if (panel.Children().IndexOf(fontCanvas, index)) panel.Children().RemoveAt(index);
            for (size_t i = 0; i < properties.size(); ++i) {
                if (IsOverride(i)) properties[i].Restore();
            }
            image.Source(nullptr);
            renderer.reset(); renderedText.clear();
            applied = false;
            updating = false;
        } catch (...) { updating = false; throw; }
    }
    void Presentation(bool custom) {
        const xaml::UIElement current = custom ? fontCanvas.as<xaml::UIElement>() : line.as<xaml::UIElement>();
        const xaml::UIElement other = custom ? line.as<xaml::UIElement>() : fontCanvas.as<xaml::UIElement>();
        uint32_t index = 0;
        if (panel.Children().IndexOf(other, index)) panel.Children().SetAt(index, current);
        else if (!panel.Children().IndexOf(current, index)) panel.Children().Append(current);
    }
    void AlignImage() noexcept {
        if (aligning || !applied || renderedFont == ClockFont::System || renderedScale <= 0) return;
        try {
            if (!image.Source() || fontCanvas.Parent() != panel) return;
            aligning = true;
            const auto origin = fontCanvas.TransformToVisual(nullptr).TransformPoint({0, 0});
            const double top = std::round((origin.Y + imageTop) * renderedScale) / renderedScale - origin.Y;
            const double left = std::round(origin.X * renderedScale) / renderedScale - origin.X;
            const auto oldTop = controls::Canvas::GetTop(image), oldLeft = controls::Canvas::GetLeft(image);
            if (!std::isfinite(oldTop) || std::abs(oldTop - top) > 0.00001) controls::Canvas::SetTop(image, top);
            if (!std::isfinite(oldLeft) || std::abs(oldLeft - left) > 0.00001) controls::Canvas::SetLeft(image, left);
        } catch (...) { /* A later layout/clock refresh retries a detached visual. */ }
        aligning = false;
    }
    void CustomFont(const std::wstring& text, ClockFont choice) {
        if (choice != renderedFont) {
            renderer.reset(); renderedText.clear(); fontResult = S_OK; renderedFont = choice;
        }
        if (choice == ClockFont::System) { Presentation(false); return; }
        try {
            if (!renderer) renderer = std::make_unique<ClockFontRenderer>(ClockFontPath(choice));
            double size = date.FontSize(), scale = 1;
            if (const auto root = panel.XamlRoot()) scale = root.RasterizationScale();
            if (date.IsTextScaleFactorEnabled()) size *= winrt::Windows::UI::ViewManagement::UISettings().TextScaleFactor();
            const std::wstring referenceFamily(line.FontFamily().Source());
            const UINT referenceWeight = line.FontWeight().Weight;
            const auto brush = date.Foreground().try_as<xaml::Media::SolidColorBrush>();
            const auto color = brush ? brush.Color() : winrt::Windows::UI::Color{255, 243, 243, 243};
            const uint32_t argb = (static_cast<uint32_t>(color.A) << 24) | (static_cast<uint32_t>(color.R) << 16)
                | (static_cast<uint32_t>(color.G) << 8) | color.B;
            if (renderedText != text || renderedSize != size || renderedScale != scale || renderedColor != argb
                || renderedFamily != referenceFamily || renderedWeight != referenceWeight) {
                const auto bitmap = renderer->Render(text, size, scale, argb, referenceFamily, referenceWeight);
                xaml::Media::Imaging::WriteableBitmap source(static_cast<int>(bitmap.width), static_cast<int>(bitmap.height));
                BYTE* pixels = nullptr;
                winrt::check_hresult(source.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&pixels));
                std::memcpy(pixels, bitmap.pixels.data(), bitmap.pixels.size() * sizeof(uint32_t)); source.Invalidate();
                image.Source(source); image.Width(bitmap.dipWidth); image.Height(bitmap.dipHeight);
                fontCanvas.Width(bitmap.dipWidth); bitmapBaseline = bitmap.baseline;
                renderedText = text; renderedSize = size; renderedScale = scale; renderedColor = argb;
                renderedFamily = referenceFamily; renderedWeight = referenceWeight;
            }
            // Keep the native Segoe line's slot. Playground uses a one-DIP
            // downward optical adjustment; Pangaia uses the native baseline.
            // Scale the adjustment with accessibility text size. Swashes and
            // descenders may paint outside it, without enlarging or recentering
            // the line box and without shrinking the letters to fit those tails.
            line.Measure({4096, 512});
            const double lineHeight = line.DesiredSize().Height, baseline = line.BaselineOffset();
            if (!std::isfinite(baseline) || lineHeight <= 0) winrt::throw_hresult(E_FAIL);
            fontCanvas.Height(lineHeight);
            imageTop = baseline - bitmapBaseline + (choice == ClockFont::Playground ? size / 12.0 : 0);
            fontResult = S_OK; Presentation(true); AlignImage();
            return;
        } catch (HRESULT hr) { fontResult = hr; }
        catch (...) { fontResult = winrt::to_hresult(); }
        // A missing/invalid external file preserves a readable single-line
        // stock-font clock. Report the failure independently of its layout.
        Presentation(false);
    }
    void Refresh(bool enabled, ClockOptions options) {
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
        line.Foreground(date.Foreground());
        line.IsTextScaleFactorEnabled(date.IsTextScaleFactorEnabled());
        CustomFont(text, options.font);
    }
    ~Impl() { Unregister(); try { Restore(); } catch (...) {} }
};
ClockView::ClockView(const controls::StackPanel& panel) : impl(std::make_unique<Impl>(panel)) {}
ClockView::~ClockView() = default;
void ClockView::Refresh(bool enabled, ClockOptions options) { impl->Refresh(enabled, options); }
HRESULT ClockView::FontResult() const noexcept { return impl->fontResult; }
void ClockView::Restore() { impl->Restore(); }
bool ClockView::Applied() const noexcept { return impl->applied; }
}

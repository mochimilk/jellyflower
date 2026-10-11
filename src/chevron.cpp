// SPDX-License-Identifier: GPL-3.0-or-later
#include "chevron.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <vector>
#include <cmath>
namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace {
constexpr double SecondaryOpacity = 0.6;
bool IsSecondary(double value) { return std::abs(value - SecondaryOpacity) < 0.000001; }
winrt::hstring ClassName(const xaml::DependencyObject& object) { return winrt::get_class_name(object); }
bool IsGlyph(const controls::TextBlock& glyph, ChevronClassName name) {
    if (glyph.Name() != L"InnerTextBlock") return false;
    struct Part { const wchar_t* type; const wchar_t* name; };
    constexpr Part path[]{
        {L"SystemTray.AdaptiveTextBlock", L"Base"},
        {L"Windows.UI.Xaml.Controls.Grid", L"ContainerGrid"},
        {L"SystemTray.TextIconContent", nullptr},
        {L"Windows.UI.Xaml.Controls.Grid", L"ContentGrid"},
        {L"Windows.UI.Xaml.Controls.ContentPresenter", L"ContentPresenter"},
        {L"Windows.UI.Xaml.Controls.Grid", L"ContainerGrid"},
        {L"SystemTray.ChevronIconView", nullptr}
    };
    xaml::DependencyObject current = glyph;
    for (const auto& part : path) {
        current = media::VisualTreeHelper::GetParent(current);
        const auto element = current.try_as<xaml::FrameworkElement>();
        if (!element || name(current) != part.type || (part.name && element.Name() != part.name)) return false;
    }
    return true;
}
}
xaml::FrameworkElement FindChevronRoot(const xaml::DependencyObject& object, ChevronClassName className) {
    if (!className) className = ClassName;
    auto current = object;
    for (unsigned depth = 0; current && depth < 16; ++depth) {
        if (className(current) == L"SystemTray.ChevronIconView") return current.try_as<xaml::FrameworkElement>();
        current = media::VisualTreeHelper::GetParent(current);
    }
    return nullptr;
}
controls::TextBlock FindChevronGlyph(const xaml::DependencyObject& object, ChevronClassName className) {
    if (!className) className = ClassName;
    const auto root = FindChevronRoot(object, className);
    if (!root) return nullptr;
    std::vector<xaml::DependencyObject> pending{root};
    for (unsigned visited = 0; !pending.empty() && visited < 128; ++visited) {
        auto item = pending.back(); pending.pop_back();
        const auto glyph = item.try_as<controls::TextBlock>();
        if (glyph && IsGlyph(glyph, className)) return glyph;
        const int count = media::VisualTreeHelper::GetChildrenCount(item);
        if (count > 64) return nullptr;
        for (int i = 0; i < count; ++i) pending.push_back(media::VisualTreeHelper::GetChild(item, i));
    }
    return nullptr;
}
struct ChevronView::Impl {
    controls::TextBlock glyph;
    winrt::Windows::Foundation::IInspectable local{nullptr};
    xaml::Data::BindingBase binding{nullptr};
    int64_t token = 0;
    bool applied = false, updating = false;
    explicit Impl(const controls::TextBlock& value) : glyph(value) {
        if (!glyph) winrt::throw_hresult(E_INVALIDARG);
        token = glyph.RegisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(),
            [this](auto&&, auto&&) { Changed(); });
    }
    void Save() {
        const auto property = xaml::UIElement::OpacityProperty();
        local = glyph.ReadLocalValue(property);
        const auto expression = glyph.GetBindingExpression(property);
        binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Changed() noexcept {
        if (!applied || updating) return;
        try {
            if (IsSecondary(glyph.Opacity())) return;
            Save(); updating = true; glyph.Opacity(SecondaryOpacity); updating = false;
        }
        catch (...) { updating = false; }
    }
    void Refresh(bool enabled) {
        if (!enabled) { Restore(); return; }
        if (applied) return;
        Save(); applied = true; updating = true;
        try { glyph.Opacity(SecondaryOpacity); updating = false; }
        catch (...) { updating = false; Restore(); throw; }
    }
    void Restore() {
        if (!applied) return;
        updating = true;
        try {
            if (IsSecondary(glyph.Opacity())) {
                const auto property = xaml::UIElement::OpacityProperty();
                glyph.ClearValue(property);
                if (binding) glyph.SetBinding(property, binding);
                else if (local != xaml::DependencyProperty::UnsetValue()) glyph.SetValue(property, local);
            }
            applied = false; updating = false;
        } catch (...) { updating = false; throw; }
    }
    ~Impl() {
        try { glyph.UnregisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(), token); } catch (...) {}
        try { Restore(); } catch (...) {}
    }
};
ChevronView::ChevronView(const controls::TextBlock& glyph) : impl(std::make_unique<Impl>(glyph)) {}
ChevronView::~ChevronView() = default;
void ChevronView::Refresh(bool enabled) { impl->Refresh(enabled); }
void ChevronView::Restore() { impl->Restore(); }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_style.hpp"
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace acrylic {
inline bool IsStartSearch(const sx::Controls::Control& control, StartClassName name = nullptr) {
    if (!control) return false;
    const auto type = name ? name(control) : winrt::get_class_name(control);
    return type == L"StartMenu.SearchBoxToggleButton" || type == L"StartDocked.SearchBoxToggleButton";
}
inline sx::FrameworkElement FindStartElement(const sx::DependencyObject& root, std::wstring_view name) {
    std::vector<sx::DependencyObject> pending{root};
    for (size_t i = 0; i < pending.size() && i < 12000; ++i) {
        if (auto element = pending[i].try_as<sx::FrameworkElement>(); element && element.Name() == name) return element;
        const auto count = sx::Media::VisualTreeHelper::GetChildrenCount(pending[i]);
        for (int child = 0; child < count && pending.size() < 12000; ++child)
            pending.push_back(sx::Media::VisualTreeHelper::GetChild(pending[i], child));
    }
    return nullptr;
}
// Each override owns its original local value/binding. Shell visual-state and
// localization changes become the new originals while the override is active.
class StartSearchValues {
    struct Item {
        sx::FrameworkElement object{nullptr};
        sx::DependencyProperty key{nullptr};
        winrt::Windows::Foundation::IInspectable local{nullptr}, custom{nullptr};
        sx::Data::BindingBase binding{nullptr};
        int64_t token = 0;
    };
    std::vector<Item> items_;
    bool writing_ = false;
    void Save(Item& item) {
        item.local = item.object.ReadLocalValue(item.key);
        const auto expression = item.object.GetBindingExpression(item.key);
        item.binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Changed(size_t index) noexcept {
        if (writing_) return;
        try {
            auto& item = items_[index]; Save(item); writing_ = true;
            item.object.SetValue(item.key, item.custom); writing_ = false;
        } catch (...) { writing_ = false; }
    }
public:
    ~StartSearchValues() { Clear(); }
    void Set(const sx::FrameworkElement& object, const sx::DependencyProperty& key,
        const winrt::Windows::Foundation::IInspectable& custom) {
        const auto index = items_.size();
        Item item{object, key, nullptr, custom}; Save(item); items_.push_back(item);
        items_.back().token = object.RegisterPropertyChangedCallback(key, [this, index](auto&&, auto&&) { Changed(index); });
        writing_ = true;
        try { object.SetValue(key, custom); writing_ = false; }
        catch (...) { writing_ = false; throw; }
    }
    void Clear() noexcept {
        writing_ = true;
        for (auto& item : items_) {
            try { if (item.token) item.object.UnregisterPropertyChangedCallback(item.key, item.token); } catch (...) {}
            try {
                item.object.ClearValue(item.key);
                if (item.binding) item.object.SetBinding(item.key, item.binding);
                else if (item.local != sx::DependencyProperty::UnsetValue()) item.object.SetValue(item.key, item.local);
            } catch (...) {}
        }
        items_.clear(); writing_ = false;
    }
};
class StartSearch {
    sx::Controls::Control button_;
    sx::Controls::Grid template_{nullptr}, pane_{nullptr}, header_{nullptr};
    sx::Controls::Border outline_{nullptr}, divider_{nullptr};
    sx::Controls::TextBlock placeholder_{nullptr};
    sx::Controls::ContentPresenter presenter_{nullptr};
    sx::Controls::Canvas iconLayer_{nullptr}, dividerLayer_{nullptr};
    sx::Controls::Canvas icon_{nullptr};
    sx::Media::TranslateTransform textShift_{nullptr}, caretShift_{nullptr};
    sx::FrameworkElement caret_{nullptr};
    winrt::weak_ref<sx::FrameworkElement> heading_;
    StartSearchValues values_;
    winrt::event_token layout_{};
    bool applied_ = false, positioning_ = false;
    bool light_ = false;
    static void Remove(const sx::Controls::Grid& parent, const sx::UIElement& child) noexcept {
        try { uint32_t index = 0; if (parent && child && parent.Children().IndexOf(child, index)) parent.Children().RemoveAt(index); } catch (...) {}
    }
    sx::Media::TranslateTransform Shift(const sx::FrameworkElement& element) {
        sx::Media::TransformGroup group;
        if (const auto original = element.RenderTransform()) group.Children().Append(original);
        sx::Media::TranslateTransform shift; group.Children().Append(shift);
        values_.Set(element, sx::UIElement::RenderTransformProperty(), group); return shift;
    }
    static sx::Controls::Canvas Layer() {
        sx::Controls::Canvas layer; layer.Width(0); layer.Height(0);
        layer.HorizontalAlignment(sx::HorizontalAlignment::Left); layer.VerticalAlignment(sx::VerticalAlignment::Top);
        layer.IsHitTestVisible(false);
        sx::Automation::AutomationProperties::SetAccessibilityView(layer, sx::Automation::Peers::AccessibilityView::Raw);
        sx::Controls::Grid::SetRowSpan(layer, 100); sx::Controls::Grid::SetColumnSpan(layer, 100);
        return layer;
    }
    bool Discover() {
        const auto children = sx::Media::VisualTreeHelper::GetChildrenCount(button_);
        if (children != 1) return false;
        const auto grid = sx::Media::VisualTreeHelper::GetChild(button_, 0).try_as<sx::Controls::Grid>();
        if (!grid) return false;
        const auto placeholder = FindStartElement(grid, L"PlaceholderText").try_as<sx::Controls::TextBlock>();
        const auto outline = FindStartElement(grid, L"BorderElement").try_as<sx::Controls::Border>();
        const auto presenter = FindStartElement(grid, L"ContentPresenter").try_as<sx::Controls::ContentPresenter>();
        if (!placeholder || !outline || !presenter) return false;
        if (applied_ && grid == template_ && placeholder == placeholder_ && outline == outline_ && presenter == presenter_) return true;
        if (applied_) Restore();
        template_ = grid; placeholder_ = placeholder; outline_ = outline; presenter_ = presenter;
        header_ = sx::Media::VisualTreeHelper::GetParent(button_).try_as<sx::Controls::Grid>();
        pane_ = nullptr;
        auto parent = sx::Media::VisualTreeHelper::GetParent(button_);
        for (unsigned depth = 0; parent && depth < 24; ++depth) {
            if (auto candidate = parent.try_as<sx::Controls::Grid>(); candidate && candidate.Name() == L"MainContent") { pane_ = candidate; break; }
            parent = sx::Media::VisualTreeHelper::GetParent(parent);
        }
        return header_ && pane_;
    }
    void Position() noexcept {
        if (!applied_ || positioning_ || button_.ActualWidth() <= 0 || pane_.ActualWidth() <= 0) return;
        positioning_ = true;
        try {
            const auto button = button_.TransformToVisual(pane_).TransformPoint({0,0});
            const auto header = header_.TransformToVisual(pane_).TransformPoint({0,0});
            // Modern Start's section inset is 64 DIP. Follow a named stock
            // heading when available, including a resized/localized layout.
            double left = 64;
            const auto heading = heading_.get();
            if (heading && heading.ActualWidth() > 0) left = heading.TransformToVisual(pane_).TransformPoint({0,0}).X;
            left = std::clamp(left, 16.0, std::max(16.0, pane_.ActualWidth() - 64));
            const double iconLeft = left - button.X;
            if (sx::Controls::Canvas::GetLeft(icon_) != iconLeft) sx::Controls::Canvas::SetLeft(icon_, iconLeft);
            const double top = (button_.ActualHeight() - 24) / 2;
            if (sx::Controls::Canvas::GetTop(icon_) != top) sx::Controls::Canvas::SetTop(icon_, top);
            const auto text = presenter_.TransformToVisual(pane_).TransformPoint({0,0});
            const double shift = left + 24 + 8 - (text.X - textShift_.X());
            if (std::abs(textShift_.X() - shift) > .01) textShift_.X(shift);
            if (caretShift_ && std::abs(caretShift_.X() - shift) > .01) caretShift_.X(shift);
            const double bottom = header_.ActualHeight() <= button_.ActualHeight() + 64
                ? header.Y + header_.ActualHeight() : button.Y + button_.ActualHeight() + 16;
            if (divider_.Width() != pane_.ActualWidth()) divider_.Width(pane_.ActualWidth());
            if (sx::Controls::Canvas::GetTop(divider_) != bottom - 1) sx::Controls::Canvas::SetTop(divider_, bottom - 1);
        } catch (...) {}
        positioning_ = false;
    }
    void Ink() {
        auto brush = placeholder_.Foreground();
        if (!brush) brush = button_.Foreground();
        const auto count = sx::Media::VisualTreeHelper::GetChildrenCount(icon_);
        for (int i = 0; i < count; ++i) if (auto shape = sx::Media::VisualTreeHelper::GetChild(icon_, i).try_as<sx::Shapes::Shape>()) shape.Stroke(brush);
        const auto stroke = MenuBorder(light_);
        divider_.Background(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,GetRValue(stroke),GetGValue(stroke),GetBValue(stroke)}));
    }
public:
    explicit StartSearch(const sx::Controls::Control& button) : button_(button) { if (!button_) winrt::throw_hresult(E_INVALIDARG); }
    StartSearch(const StartSearch&) = delete;
    StartSearch& operator=(const StartSearch&) = delete;
    ~StartSearch() { Restore(); }
    void Refresh(bool enabled, bool light) {
        if (!enabled) { Restore(); return; }
        if (!Discover()) { if (applied_) Restore(); return; }
        if (!heading_.get()) heading_ = FindStartElement(pane_, L"PinnedListHeaderText");
        light_ = light;
        if (!applied_) {
            try {
                sx::Media::SolidColorBrush clear(winrt::Windows::UI::Color{0,0,0,0});
                values_.Set(button_, sx::Controls::Control::BackgroundProperty(), clear);
                values_.Set(button_, sx::Controls::Control::BorderBrushProperty(), clear);
                values_.Set(button_, sx::Controls::Control::BorderThicknessProperty(), winrt::box_value(sx::Thickness{}));
                values_.Set(outline_, sx::Controls::Border::BackgroundProperty(), clear);
                values_.Set(outline_, sx::Controls::Border::BorderBrushProperty(), clear);
                values_.Set(outline_, sx::Controls::Border::BorderThicknessProperty(), winrt::box_value(sx::Thickness{}));
                values_.Set(placeholder_, sx::Controls::TextBlock::TextProperty(), winrt::box_value(L"search"));
                values_.Set(button_, sx::Automation::AutomationProperties::NameProperty(), winrt::box_value(L"search"));
                for (const auto name : {L"SearchGlyph",L"SearchIconOff",L"SearchIconOn",L"UnderlineContainer",L"BorderUnderline"})
                    if (const auto element = FindStartElement(template_, name)) values_.Set(element, sx::UIElement::OpacityProperty(), winrt::box_value(0.0));
                textShift_ = Shift(presenter_);
                caret_ = FindStartElement(template_, L"TextCaret");
                if (caret_) caretShift_ = Shift(caret_);
                icon_ = sx::Markup::XamlReader::Load(LR"(<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="24" Height="24"><Path Data="M21,21 L16.66,16.66" StrokeThickness="2" StrokeStartLineCap="Round" StrokeEndLineCap="Round"/><Ellipse Canvas.Left="3" Canvas.Top="3" Width="16" Height="16" StrokeThickness="2"/></Canvas>)").as<sx::Controls::Canvas>();
                iconLayer_ = Layer(); iconLayer_.Name(L"JellyflowerSearchIcon"); iconLayer_.Children().Append(icon_);
                dividerLayer_ = Layer(); dividerLayer_.Name(L"JellyflowerSearchDivider");
                divider_ = sx::Controls::Border(); divider_.Height(1); sx::Controls::Canvas::SetLeft(divider_,0); dividerLayer_.Children().Append(divider_);
                template_.Children().Append(iconLayer_); pane_.Children().Append(dividerLayer_);
                applied_ = true;
                layout_ = button_.LayoutUpdated([this](auto&&, auto&&) { Position(); });
            } catch (...) { Restore(); throw; }
        }
        Ink(); Position();
    }
    void Restore() noexcept {
        if (layout_.value) { try { button_.LayoutUpdated(layout_); } catch (...) {} layout_ = {}; }
        applied_ = false;
        Remove(template_, iconLayer_); Remove(pane_, dividerLayer_); values_.Clear();
        iconLayer_ = nullptr; dividerLayer_ = nullptr; icon_ = nullptr; divider_ = nullptr;
        textShift_ = nullptr; caretShift_ = nullptr; caret_ = nullptr;
        heading_ = nullptr;
    }
    bool Applied() const noexcept { return applied_; }
};
}

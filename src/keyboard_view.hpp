// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keyboard_layout.hpp"
#include "start_properties.hpp"
#include <winrt/Windows.UI.ViewManagement.h>
#include <cwctype>
#include <string>

// Touch keyboard element names were read from the live visual tree of
// TextInputHost.exe (Windows 11 25H2, build 26200) with a read-only XAML
// diagnostics probe. See THIRD_PARTY_NOTICES.md.
namespace acrylic {
inline constexpr wchar_t KeyboardGridType[] = L"WindowsInternal.ComposableShell.Experiences.TextInputUndocked.Controls.KeyboardGrid";

// The keys' panel: Grid TEMPLATE_PART_KeyGrid directly inside the keyboard's KeyboardGrid.
inline bool IsKeyboardKeyGrid(const sx::Controls::Grid& grid, StartClassName name = nullptr) {
    if (!grid || grid.Name() != L"TEMPLATE_PART_KeyGrid") return false;
    const auto parent = sx::Media::VisualTreeHelper::GetParent(grid);
    return parent && (name ? name(parent) : winrt::get_class_name(parent)) == KeyboardGridType;
}
// One visible character: a letter, digit or punctuation mark in any script.
// Icon glyphs (private use area) and words such as "&123" or "Ctrl" are not.
inline bool IsKeyboardCharacter(std::wstring_view text) {
    if (text.size() == 2) return text[0] >= 0xD800 && text[0] <= 0xDBFF && text[1] >= 0xDC00 && text[1] <= 0xDFFF;
    if (text.size() != 1) return false;
    const wchar_t c = text[0];
    return c > 0x20 && !(c >= 0x7F && c < 0xA0) && !(c >= 0xE000 && c <= 0xF8FF) && !std::iswspace(c);
}
// A letter in any script (Latin, Hangul jamo, kana...), not punctuation.
inline bool IsKeyboardLetter(std::wstring_view text) {
    WORD type = 0;
    return text.size() == 1 && IsKeyboardCharacter(text) && GetStringTypeW(CT_CTYPE1, text.data(), 1, &type) && (type & C1_ALPHA);
}
inline std::wstring KeyboardKeyText(const sx::FrameworkElement& key, std::wstring_view part) {
    const auto element = FindStartElement(key, part);
    const auto text = element ? element.try_as<sx::Controls::TextBlock>() : nullptr;
    return text ? std::wstring(text.Text()) : std::wstring();
}
// Key kind and role from the labels the keyboard shows. The language key
// carries its name (ENG, FRA, 한) in the secondary label and none in the main one.
inline KeyboardKeySlot KeyboardKeyFor(const sx::FrameworkElement& key) {
    KeyboardKeySlot slot;
    const auto label = KeyboardKeyText(key, L"TEMPLATE_PART_KeyLabel");
    if (IsKeyboardCharacter(label)) {
        slot.kind = KeyboardKeyKind::Character;
        slot.letter = IsKeyboardLetter(label);
        if (label.size() == 1) slot.symbol = label[0];
    }
    else if (!label.empty() && std::all_of(label.begin(), label.end(), [](wchar_t c) { return std::iswspace(c) != 0; })) slot.kind = KeyboardKeyKind::Space;
    else if (label == L"Ctrl" || label == L"Strg") slot.role = KeyboardKeyRole::Control;
    else if (label == L"") slot.role = KeyboardKeyRole::Microphone;  // Segoe Fluent Icons microphone
    else if (label.empty() && !KeyboardKeyText(key, L"TEMPLATE_PART_KeySecondaryLabel").empty()) slot.role = KeyboardKeyRole::Language;
    return slot;
}

// Key hints at 80% of the keyboard's size, in Windows 11's tertiary text
// color: the keyboard's own ink at TextFillColorTertiary's opacity (#8BFFFFFF
// in dark mode, #72000000 in light), so custom keyboard themes keep their hue.
// Both follow the keyboard's later changes (theme, text size) as transforms.
inline winrt::Windows::Foundation::IInspectable KeyboardHintSize(const winrt::Windows::Foundation::IInspectable& value) {
    return winrt::box_value(winrt::unbox_value_or<double>(value, 12.0) * .8);
}
inline winrt::Windows::Foundation::IInspectable KeyboardHintInk(const winrt::Windows::Foundation::IInspectable& value) {
    const auto brush = value.try_as<sx::Media::SolidColorBrush>();
    if (!brush) return value;
    auto color = brush.Color();
    const bool light = color.R + color.G + color.B > 384;  // light ink: dark mode
    color.A = light ? 0x8B : 0x72;
    return sx::Media::SolidColorBrush(color);
}

// What the focused text field holds, as reported by the controller through
// UI Automation: unknown fields (passwords, documents, apps that do not
// report text) keep the stock return key.
enum class KeyboardField : LONG { Unknown = 0, Empty = 1, Text = 2 };

// Applies ArrangeKeyboard to the live keys. The keyboard positions every key
// with a local Margin, Width and Height (and Visibility), and rewrites them
// when it changes page, size or orientation. Those rewrites become the new
// originals, per property, and are arranged again from LayoutUpdated before
// the frame is drawn. Touch input follows the moved elements (verified by
// tapping moved keys). Gaps and corners change only each key's visual
// bounds and background, never its hit-tested slot.
class KeyboardMirror {
    struct Geometry { double left = 0, top = 0, width = 0, height = 0; bool visible = true; };
    struct Key {
        sx::FrameworkElement element{nullptr};
        Geometry original, applied;
        bool written = false;
        sx::FrameworkElement bounds{nullptr};
        sx::Controls::Border background{nullptr};
        bool parts = false, hints = false;
    };
    sx::Controls::Grid grid_;
    std::vector<Key> keys_;
    StartSearchValues looks_;
    sx::FrameworkElement ribbon_{nullptr}, ribbonStart_{nullptr}, ribbonEnd_{nullptr};
    double edge_ = 0;
    bool ribbonPending_ = false;
    // Return key: dimmed and untappable for an empty field, accent for text.
    StartSearchValues enterLooks_;
    sx::FrameworkElement enterKey_{nullptr};
    KeyboardField field_ = KeyboardField::Unknown, styledField_ = KeyboardField::Unknown;
    winrt::Windows::UI::Color styledAccent_{};
    winrt::event_token layout_{};
    bool enabled_ = false, applied_ = false, busy_ = false, pending_ = false, failed_ = false, compact_ = false, symbols_ = false;
    unsigned burst_ = 0, arrangements_ = 0;

    static Geometry Measure(const sx::FrameworkElement& element) {
        const auto margin = element.Margin();
        return {margin.Left, margin.Top, element.Width(), element.Height(), element.Visibility() == sx::Visibility::Visible};
    }
    static bool Near(double a, double b) { return (std::isnan(a) && std::isnan(b)) || std::abs(a - b) < .01; }
    static void Write(const sx::FrameworkElement& element, const Geometry& value) {
        auto margin = element.Margin();
        if (!Near(margin.Left, value.left) || !Near(margin.Top, value.top)) { margin.Left = value.left; margin.Top = value.top; element.Margin(margin); }
        if (!Near(element.Width(), value.width)) element.Width(value.width);
        if (!Near(element.Height(), value.height)) element.Height(value.height);
        const auto visibility = value.visible ? sx::Visibility::Visible : sx::Visibility::Collapsed;
        if (element.Visibility() != visibility) element.Visibility(visibility);
    }
    // Position, width, height and visibility are owned separately: the
    // keyboard may rewrite one of them, and the others must still restore.
    static bool Adopt(Geometry& original, const Geometry& current, const Geometry& reference) {
        bool changed = false;
        if (!Near(current.left, reference.left) || !Near(current.top, reference.top)) { original.left = current.left; original.top = current.top; changed = true; }
        if (!Near(current.width, reference.width)) { original.width = current.width; changed = true; }
        if (!Near(current.height, reference.height)) { original.height = current.height; changed = true; }
        if (current.visible != reference.visible) { original.visible = current.visible; changed = true; }
        return changed;
    }
    static void Return(const Key& key) {
        if (!key.written) return;
        const auto current = Measure(key.element);
        auto target = current;
        if (Near(current.left, key.applied.left) && Near(current.top, key.applied.top)) { target.left = key.original.left; target.top = key.original.top; }
        if (Near(current.width, key.applied.width)) target.width = key.original.width;
        if (Near(current.height, key.applied.height)) target.height = key.original.height;
        if (current.visible == key.applied.visible) target.visible = key.original.visible;
        Write(key.element, target);
    }
    // The language key shows a globe, as on a tablet, instead of the input
    // language's name (ENG, 한). The name lives in the secondary label with an
    // empty main label, which keeps the key recognisable on later passes; the
    // keyboard's own updates to it (a language switch) are overridden again.
    // The glyph matches the size of the other icon keys (backspace, mic).
    void Globe(Key& key) {
        const auto element = FindStartElement(key.element, L"TEMPLATE_PART_KeySecondaryLabel");
        const auto name = element ? element.try_as<sx::Controls::TextBlock>() : nullptr;
        // Once in place, the overrides re-apply themselves when the keyboard
        // writes the label again; setting them each pass would only churn.
        if (!name || looks_.Contains(name, sx::Controls::TextBlock::TextProperty())) return;
        double size = 16;
        for (const auto& other : keys_) {
            const auto labelElement = FindStartElement(other.element, L"TEMPLATE_PART_KeyLabel");
            const auto label = labelElement ? labelElement.try_as<sx::Controls::TextBlock>() : nullptr;
            const auto text = label ? std::wstring(label.Text()) : std::wstring();
            if (text.size() == 1 && text[0] >= 0xE000 && text[0] <= 0xF8FF) { size = label.FontSize(); break; }
        }
        looks_.Set(name, sx::Controls::TextBlock::FontFamilyProperty(), sx::Media::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
        looks_.Set(name, sx::Controls::TextBlock::FontSizeProperty(), winrt::box_value(size));
        looks_.Set(name, sx::Controls::TextBlock::TextProperty(), winrt::box_value(L""));
    }
    // Windows 11 tokens on the keyboard's own theme (light ink: dark mode):
    // AccentFillColorDefault (accent light 2 in dark mode, dark 1 in light)
    // with TextOnAccentFillColorPrimary, or TextFillColorDisabled.
    void Enter() {
        auto field = applied_ && enterKey_ ? field_ : KeyboardField::Unknown;
        winrt::Windows::UI::Color accent{};
        sx::Controls::TextBlock label{nullptr};
        sx::Controls::Border background{nullptr};
        bool dark = true;
        if (field != KeyboardField::Unknown) {
            const auto labelElement = FindStartElement(enterKey_, L"TEMPLATE_PART_KeyLabel");
            label = labelElement ? labelElement.try_as<sx::Controls::TextBlock>() : nullptr;
            const auto backgroundElement = FindStartElement(enterKey_, L"TEMPLATE_PART_KeyBackgroundBorder");
            background = backgroundElement ? backgroundElement.try_as<sx::Controls::Border>() : nullptr;
            if (label) {
                const auto ink = winrt::unbox_value_or<sx::Media::Brush>(enterLooks_.Original(label, sx::Controls::TextBlock::ForegroundProperty()), nullptr);
                if (const auto solid = ink ? ink.try_as<sx::Media::SolidColorBrush>() : nullptr) dark = solid.Color().R + solid.Color().G + solid.Color().B > 384;
            }
            if (field == KeyboardField::Text)
                accent = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
                    dark ? winrt::Windows::UI::ViewManagement::UIColorType::AccentLight2 : winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
        }
        if (!label) field = KeyboardField::Unknown;
        if (field == styledField_ && (field != KeyboardField::Text || accent == styledAccent_)) return;
        enterLooks_.Clear();
        styledField_ = field;
        styledAccent_ = accent;
        if (field == KeyboardField::Unknown) return;
        if (field == KeyboardField::Empty) {
            const winrt::Windows::UI::Color disabled = dark ? winrt::Windows::UI::Color{0x5D, 255, 255, 255} : winrt::Windows::UI::Color{0x5C, 0, 0, 0};
            enterLooks_.Set(label, sx::Controls::TextBlock::ForegroundProperty(), sx::Media::SolidColorBrush(disabled));
            enterLooks_.Set(enterKey_, sx::UIElement::IsHitTestVisibleProperty(), winrt::box_value(false));
        } else {
            const winrt::Windows::UI::Color onAccent = dark ? winrt::Windows::UI::Color{255, 0, 0, 0} : winrt::Windows::UI::Color{255, 255, 255, 255};
            if (background) enterLooks_.Set(background, sx::Controls::Border::BackgroundProperty(), sx::Media::SolidColorBrush(accent));
            enterLooks_.Set(label, sx::Controls::TextBlock::ForegroundProperty(), sx::Media::SolidColorBrush(onAccent));
        }
    }
    void ResetEnter() noexcept {
        enterLooks_.Clear();
        styledField_ = KeyboardField::Unknown;
        enterKey_ = nullptr;
    }
    void Reset() noexcept {
        ResetEnter();
        for (auto& key : keys_) {
            try { Return(key); } catch (...) {}
            key.written = key.hints = false;
        }
        looks_.Clear();
        applied_ = ribbonPending_ = false;
    }
    // Visual gap and corners for one key: a margin on its visual bounds and a
    // radius on its background. The template's own inset is measured from the
    // last layout pass so the gap is exact; until then, retry next pass.
    bool Style(Key& key, const KeyboardKeyRect& rect, double radius) {
        if (!key.parts) {
            key.bounds = FindStartElement(key.element, L"TEMPLATE_PART_KeyVisualBounds");
            const auto background = FindStartElement(key.element, L"TEMPLATE_PART_KeyBackgroundBorder");
            key.background = background ? background.try_as<sx::Controls::Border>() : nullptr;
            key.parts = true;
        }
        if (key.background) looks_.Update(key.background, sx::Controls::Border::CornerRadiusProperty(), winrt::box_value(sx::CornerRadius{radius, radius, radius, radius}));
        // Hints (the digits over q..p, the marks over , . ') are drawn smaller
        // and in the tertiary text color. The language key keeps its name
        // (ENG) in the same part, with no main label; that one is left alone.
        if (!key.hints) {
            key.hints = true;
            if (!KeyboardKeyText(key.element, L"TEMPLATE_PART_KeyLabel").empty())
                for (const auto* part : {L"TEMPLATE_PART_KeySecondaryLabel", L"TEMPLATE_PART_KeyThirdLabel"}) {
                    const auto element = FindStartElement(key.element, part);
                    const auto hint = element ? element.try_as<sx::Controls::TextBlock>() : nullptr;
                    if (!hint) continue;
                    // A page switch removes and re-adds the same key elements;
                    // an override that is already in place must not be stacked
                    // (that would shrink the hint again and lose its original).
                    if (!looks_.Contains(hint, sx::Controls::TextBlock::FontSizeProperty()))
                        looks_.Set(hint, sx::Controls::TextBlock::FontSizeProperty(), nullptr, KeyboardHintSize);
                    if (!looks_.Contains(hint, sx::Controls::TextBlock::ForegroundProperty()))
                        looks_.Set(hint, sx::Controls::TextBlock::ForegroundProperty(), nullptr, KeyboardHintInk);
                }
        }
        if (!key.bounds) return true;
        const double width = key.element.ActualWidth(), height = key.element.ActualHeight();
        const double innerWidth = key.bounds.ActualWidth(), innerHeight = key.bounds.ActualHeight();
        if (!(width > 0) || !(height > 0) || !(innerWidth > 0) || !(innerHeight > 0)) return false;
        const auto margin = key.bounds.Margin();
        const double baseX = (std::max)(0.0, (width - innerWidth - margin.Left - margin.Right) / 2);
        const double baseY = (std::max)(0.0, (height - innerHeight - margin.Top - margin.Bottom) / 2);
        const sx::Thickness gap{(std::max)(0.0, rect.insetLeft - baseX), (std::max)(0.0, rect.insetTop - baseY),
            (std::max)(0.0, rect.insetRight - baseX), (std::max)(0.0, rect.insetBottom - baseY)};
        looks_.Update(key.bounds, sx::FrameworkElement::MarginProperty(), winrt::box_value(gap));
        return true;
    }
    // The suggestion bar above the keys (settings and emoji on the left,
    // undock and close on the right) gets a margin that lines its outer icons
    // up with the outer keys' visible edges. Its parts sit beside the key grid:
    // KeyGrid > KeyboardGrid > InputPanelGrid > CandidateBar > ... > WebUIRoot.
    // Returns false while the bar is not laid out yet, to be retried.
    bool Ribbon(double edge) {
        if (!ribbon_ || !sx::Media::VisualTreeHelper::GetParent(ribbon_)) {
            const auto keyboard = sx::Media::VisualTreeHelper::GetParent(grid_);
            const auto panel = keyboard ? sx::Media::VisualTreeHelper::GetParent(keyboard) : nullptr;
            const auto bar = panel ? FindStartElement(panel, L"TEMPLATE_PART_CandidateBar") : nullptr;
            ribbon_ = bar ? FindStartElement(bar, L"WebUIRoot") : nullptr;
            ribbonStart_ = ribbon_ ? FindStartElement(ribbon_, L"KeyboardSettingsMenuButton") : nullptr;
            ribbonEnd_ = ribbon_ ? FindStartElement(ribbon_, L"SuggestionUIContainerCloseButton") : nullptr;
        }
        if (!ribbon_) return true;  // no suggestion bar on this keyboard
        if (!(ribbon_.ActualWidth() > 0) || !(grid_.ActualWidth() > 0)) return false;
        const auto parent = sx::Media::VisualTreeHelper::GetParent(ribbon_).try_as<sx::UIElement>();
        if (!parent) return false;
        const auto current = ribbon_.Margin();
        const auto stock = winrt::unbox_value_or<sx::Thickness>(looks_.Original(ribbon_, sx::FrameworkElement::MarginProperty()), current);
        const double gridLeft = grid_.TransformToVisual(parent).TransformPoint({0, 0}).X, gridRight = gridLeft + grid_.ActualWidth();
        const double slotLeft = ribbon_.TransformToVisual(parent).TransformPoint({0, 0}).X - current.Left;
        const double slotRight = slotLeft + current.Left + ribbon_.ActualWidth() + current.Right;
        const double startInset = ribbonStart_ ? ribbonStart_.TransformToVisual(ribbon_).TransformPoint({0, 0}).X : 0;
        const double endInset = ribbonEnd_ ? ribbon_.ActualWidth() - (ribbonEnd_.TransformToVisual(ribbon_).TransformPoint({0, 0}).X + ribbonEnd_.ActualWidth()) : 0;
        const sx::Thickness margin{(std::max)(0.0, gridLeft - slotLeft + edge - startInset), stock.Top,
            (std::max)(0.0, slotRight - gridRight + edge - endInset), stock.Bottom};
        if (!Near(current.Left, margin.Left) || !Near(current.Right, margin.Right) || !looks_.Contains(ribbon_, sx::FrameworkElement::MarginProperty()))
            looks_.Update(ribbon_, sx::FrameworkElement::MarginProperty(), winrt::box_value(margin));
        return true;
    }
    void Arrange() {
        pending_ = false;
        std::vector<KeyboardKeySlot> slots;
        std::vector<size_t> visible;
        double origin = 0, extent = 0;
        for (size_t i = 0; i < keys_.size(); ++i) {
            const auto& element = keys_[i].element;
            const auto& stock = keys_[i].original;
            if (!stock.visible) continue;  // keys the keyboard itself hides stay out
            // Labels exist once the key's template is applied; until then, retry.
            if (!sx::Media::VisualTreeHelper::GetChildrenCount(element)) { pending_ = true; Reset(); return; }
            auto slot = KeyboardKeyFor(element);
            slot.left = stock.left; slot.top = stock.top; slot.width = stock.width; slot.height = stock.height;
            origin = visible.empty() ? slot.left : (std::min)(origin, slot.left);
            extent = (std::max)(extent, slot.left + slot.width);
            slots.push_back(slot);
            visible.push_back(i);
        }
        for (auto& slot : slots) slot.left -= origin;
        const double scale = grid_.XamlRoot() ? grid_.XamlRoot().RasterizationScale() : 1.0;
        const auto arrangement = ArrangeKeyboard(slots, extent - origin, scale);
        if (arrangement.keys.empty()) { Reset(); return; }
        // The keyboard should settle after one rewrite. If it keeps rewriting
        // our values, leave it stock rather than fight its layout every frame.
        if (++burst_ > 24) { Reset(); failed_ = true; return; }
        for (size_t i = 0; i < visible.size(); ++i) {
            auto& key = keys_[visible[i]];
            const auto& rect = arrangement.keys[i];
            const Geometry target{rect.left + origin, rect.top, rect.width, rect.height, rect.visible};
            Write(key.element, target);
            key.applied = target;
            key.written = true;
            if (rect.visible && !Style(key, rect, arrangement.radius)) pending_ = true;
            if (rect.visible && slots[i].role == KeyboardKeyRole::Language) Globe(key);
        }
        edge_ = arrangement.edge;
        ribbonPending_ = !Ribbon(edge_);
        const auto enter = arrangement.enter < visible.size() ? keys_[visible[arrangement.enter]].element : nullptr;
        if (enter != enterKey_) { ResetEnter(); enterKey_ = enter; }
        compact_ = arrangement.compact;
        symbols_ = arrangement.symbols;
        applied_ = true;
        ++arrangements_;
        Enter();
    }
    void Sync() {
        if (busy_ || failed_) return;
        busy_ = true;
        struct Clear { bool& value; ~Clear() { value = false; } } clear{busy_};
        std::vector<sx::FrameworkElement> children;
        for (const auto& child : grid_.Children())
            if (const auto element = child.try_as<sx::FrameworkElement>()) children.push_back(element);
        // Unchanged keys are not re-evaluated, so a page left stock costs one
        // comparison per key on each layout pass.
        bool dirty = pending_;
        for (auto it = keys_.begin(); it != keys_.end();) {
            if (std::find(children.begin(), children.end(), it->element) != children.end()) { ++it; continue; }
            Return(*it);
            it = keys_.erase(it);
            dirty = true;
        }
        for (const auto& child : children) {
            const auto current = Measure(child);
            auto found = std::find_if(keys_.begin(), keys_.end(), [&](const Key& key) { return key.element == child; });
            if (found == keys_.end()) { Key key; key.element = child; key.original = current; keys_.push_back(key); dirty = true; continue; }
            // A value other than ours is the keyboard's own layout pass.
            const auto reference = found->written ? found->applied : found->original;
            if (Adopt(found->original, current, reference)) dirty = true;
        }
        if (dirty) Arrange();
        else if (applied_ && ribbonPending_) ribbonPending_ = !Ribbon(edge_);
    }
    void Unsubscribe() noexcept {
        if (!layout_) return;
        try { grid_.LayoutUpdated(layout_); } catch (...) {}
        layout_ = {};
    }
public:
    explicit KeyboardMirror(const sx::Controls::Grid& grid) : grid_(grid) {}
    ~KeyboardMirror() { Restore(); }
    KeyboardMirror(const KeyboardMirror&) = delete;
    KeyboardMirror& operator=(const KeyboardMirror&) = delete;
    void Refresh(bool enabled, KeyboardField field = KeyboardField::Unknown) {
        burst_ = 0;
        if (!enabled) { Restore(); return; }
        if (failed_) return;
        field_ = field;
        if (!enabled_) {
            enabled_ = true;
            layout_ = grid_.LayoutUpdated([this](auto&&, auto&&) {
                try { Sync(); } catch (...) { Reset(); failed_ = true; }
            });
        }
        try { Sync(); Enter(); } catch (...) { Reset(); failed_ = true; throw; }
    }
    void Restore() noexcept {
        Unsubscribe();
        Reset();
        keys_.clear();
        enabled_ = pending_ = failed_ = false;
    }
    bool Applied() const noexcept { return applied_; }
    bool Compact() const noexcept { return compact_; }
    bool Symbols() const noexcept { return symbols_; }
    KeyboardField EnterStyle() const noexcept { return styledField_; }
    bool Failed() const noexcept { return failed_; }
    unsigned Arrangements() const noexcept { return arrangements_; }
    const sx::Controls::Grid& Element() const noexcept { return grid_; }
};
}

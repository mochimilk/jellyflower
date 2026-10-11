// SPDX-License-Identifier: GPL-3.0-or-later
#include "volume.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <vector>

namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace {
winrt::hstring ClassName(const xaml::DependencyObject& object) { return winrt::get_class_name(object); }
bool Speaker(const winrt::hstring& text) {
    if (text.size() != 1) return false;
    switch (text[0]) {
    case 0xE74F: case 0xE767: // Mute / Volume
    case 0xE992: case 0xE993: case 0xE994: case 0xE995: return true; // Volume0..3
    default: return false;
    }
}
bool HasSpeaker(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> pending{root};
    for (unsigned visited = 0; !pending.empty() && visited < 64; ++visited) {
        auto item = pending.back(); pending.pop_back();
        if (const auto text = item.try_as<controls::TextBlock>(); text && Speaker(text.Text())) return true;
        if (const auto icon = item.try_as<controls::FontIcon>(); icon && Speaker(icon.Glyph())) return true;
        const int count = media::VisualTreeHelper::GetChildrenCount(item);
        if (count > 64) return false;
        for (int i = 0; i < count; ++i) pending.push_back(media::VisualTreeHelper::GetChild(item, i));
    }
    return false;
}
bool IsVolumeSlot(const controls::Grid& grid, XamlClassName name) {
    auto outer = media::VisualTreeHelper::GetParent(grid);
    if (!outer || name(outer) != L"Windows.UI.Xaml.Controls.Grid") return false;
    auto icon = media::VisualTreeHelper::GetParent(outer);
    if (!icon || name(icon) != L"SystemTray.IconView") return false;
    auto presenter = media::VisualTreeHelper::GetParent(icon);
    if (!presenter || name(presenter) != L"Windows.UI.Xaml.Controls.ContentPresenter") return false;
    auto stack = media::VisualTreeHelper::GetParent(presenter);
    if (!stack || name(stack) != L"Windows.UI.Xaml.Controls.StackPanel"
        || media::VisualTreeHelper::GetChildrenCount(stack) < 2
        || media::VisualTreeHelper::GetChild(stack, 1) != presenter) return false;
    auto items = media::VisualTreeHelper::GetParent(stack);
    if (!items || name(items) != L"Windows.UI.Xaml.Controls.ItemsPresenter") return false;
    auto content = media::VisualTreeHelper::GetParent(items);
    if (!content || name(content) != L"Windows.UI.Xaml.Controls.ContentPresenter") return false;
    auto root = media::VisualTreeHelper::GetParent(content);
    if (!root || name(root) != L"Windows.UI.Xaml.Controls.Grid") return false;
    auto button = media::VisualTreeHelper::GetParent(root);
    const auto element = button.try_as<xaml::FrameworkElement>();
    return element && name(button) == L"SystemTray.OmniButton" && element.Name() == L"ControlCenterButton"
        && HasSpeaker(grid);
}
}
xaml::FrameworkElement FindVolumeRoot(const xaml::DependencyObject& object, XamlClassName className) {
    if (!className) className = ClassName;
    auto current = object;
    for (unsigned depth = 0; current && depth < 16; ++depth) {
        const auto element = current.try_as<xaml::FrameworkElement>();
        if (element && className(current) == L"SystemTray.OmniButton" && element.Name() == L"ControlCenterButton") return element;
        current = media::VisualTreeHelper::GetParent(current);
    }
    return nullptr;
}
controls::Grid FindVolumeGrid(const xaml::DependencyObject& object, XamlClassName className) {
    if (!className) className = ClassName;
    const auto root = FindVolumeRoot(object, className);
    if (!root) return nullptr;
    std::vector<xaml::DependencyObject> pending{root};
    for (unsigned visited = 0; !pending.empty() && visited < 256; ++visited) {
        auto item = pending.back(); pending.pop_back();
        const auto grid = item.try_as<controls::Grid>();
        if (grid && IsVolumeSlot(grid, className)) return grid;
        const int count = media::VisualTreeHelper::GetChildrenCount(item);
        if (count > 64) return nullptr;
        for (int i = 0; i < count; ++i) pending.push_back(media::VisualTreeHelper::GetChild(item, i));
    }
    return nullptr;
}
struct VolumeView::Impl {
    controls::Grid grid;
    winrt::Windows::Foundation::IInspectable local{nullptr};
    xaml::Data::BindingBase binding{nullptr};
    int64_t token = 0;
    bool applied = false, updating = false;
    explicit Impl(const controls::Grid& value) : grid(value) {
        if (!grid) winrt::throw_hresult(E_INVALIDARG);
        token = grid.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(),
            [this](auto&&, auto&&) { Changed(); });
    }
    void Save() {
        local = grid.ReadLocalValue(xaml::UIElement::VisibilityProperty());
        const auto expression = grid.GetBindingExpression(xaml::UIElement::VisibilityProperty());
        binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Changed() noexcept {
        if (!applied || updating) return;
        try {
            if (grid.Visibility() == xaml::Visibility::Collapsed) return;
            Save();
            updating = true;
            grid.Visibility(xaml::Visibility::Collapsed);
            updating = false;
        } catch (...) { updating = false; }
    }
    void Restore() {
        if (!applied) return;
        updating = true;
        try {
            if (grid.Visibility() == xaml::Visibility::Collapsed) {
                const auto property = xaml::UIElement::VisibilityProperty();
                grid.ClearValue(property);
                if (binding) grid.SetBinding(property, binding);
                else if (local != xaml::DependencyProperty::UnsetValue()) grid.SetValue(property, local);
            }
            applied = false;
            updating = false;
        } catch (...) { updating = false; throw; }
    }
    void Refresh(bool enabled) {
        if (!enabled) { Restore(); return; }
        if (applied) return;
        Save();
        applied = true;
        updating = true;
        try { grid.Visibility(xaml::Visibility::Collapsed); updating = false; }
        catch (...) { updating = false; Restore(); throw; }
    }
    ~Impl() {
        try { grid.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), token); } catch (...) {}
        try { Restore(); } catch (...) {}
    }
};
VolumeView::VolumeView(const controls::Grid& grid) : impl(std::make_unique<Impl>(grid)) {}
VolumeView::~VolumeView() = default;
void VolumeView::Refresh(bool enabled) { impl->Refresh(enabled); }
void VolumeView::Restore() { impl->Restore(); }
bool VolumeView::Applied() const noexcept { return impl->applied; }
}

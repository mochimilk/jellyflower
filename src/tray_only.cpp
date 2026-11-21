// SPDX-License-Identifier: GPL-3.0-or-later
// Notification-area element names were reviewed in Ramen Software's Windows 11
// taskbar styling guide and m417z's Taskbar Notification Icon Spacing mod.
// See THIRD_PARTY_NOTICES.md for the pinned sources.
#include "tray_only.hpp"
#include "localization.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <algorithm>
#include <vector>

namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace media = xaml::Media;
namespace foundation = winrt::Windows::Foundation;
namespace {
constexpr unsigned MaxVisited = 512;
constexpr int MaxChildren = 64;
winrt::hstring ClassName(const xaml::DependencyObject& object) { return winrt::get_class_name(object); }
bool IsCollapsed(const foundation::IInspectable& value) {
    if (!value) return false;
    if (const auto typed = value.try_as<foundation::IReference<xaml::Visibility>>()) return typed.Value() == xaml::Visibility::Collapsed;
    if (const auto number = value.try_as<foundation::IReference<int32_t>>()) return number.Value() == static_cast<int32_t>(xaml::Visibility::Collapsed);
    return false;
}
winrt::hstring IconName(const xaml::FrameworkElement& view) {
    auto name = xaml::Automation::AutomationProperties::GetName(view);
    if (!name.empty()) return name;
    // Some shell builds compute the name in the icon's automation peer instead.
    try {
        if (const auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(view)) return peer.GetName();
    } catch (...) {}
    return {};
}
// One collapsed part. The latest stock local value or binding is kept so a
// shell update while hidden is what comes back on restore.
struct Part {
    xaml::FrameworkElement element;
    foundation::IInspectable local{nullptr};
    xaml::Data::BindingBase binding{nullptr};
    int64_t token = 0;
    bool updating = false;
    explicit Part(const xaml::FrameworkElement& value) : element(value) {
        Save();
        token = element.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), [this](auto&&, auto&&) { Changed(); });
        try { Collapse(); }
        catch (...) { Unwatch(); throw; }
    }
    ~Part() { Unwatch(); }
    Part(const Part&) = delete;
    Part& operator=(const Part&) = delete;
    void Unwatch() noexcept {
        if (!token) return;
        try { element.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), token); } catch (...) {}
        token = 0;
    }
    void Save() {
        const auto property = xaml::UIElement::VisibilityProperty();
        local = element.ReadLocalValue(property);
        const auto expression = element.GetBindingExpression(property);
        binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Collapse() {
        updating = true;
        try { element.Visibility(xaml::Visibility::Collapsed); }
        catch (...) { updating = false; throw; }
        updating = false;
    }
    bool Ours() const { return IsCollapsed(element.ReadLocalValue(xaml::UIElement::VisibilityProperty())); }
    // The shell wrote a new value or binding update: keep it as the stock
    // value and hide again. If our value is still in place, a style or
    // animation outranks it and the saved stock value stays correct.
    void Ensure() {
        if (element.Visibility() == xaml::Visibility::Collapsed || Ours()) return;
        Save(); Collapse();
    }
    void Changed() noexcept {
        if (updating) return;
        try { Ensure(); } catch (...) { updating = false; }
    }
    void Restore() {
        const auto property = xaml::UIElement::VisibilityProperty();
        if (!Ours()) return;
        updating = true;
        try {
            element.ClearValue(property);
            if (binding) element.SetBinding(property, binding);
            else if (local != xaml::DependencyProperty::UnsetValue()) element.SetValue(property, local);
        } catch (...) { updating = false; throw; }
        updating = false;
    }
};
}
xaml::FrameworkElement FindTrayFrame(const xaml::DependencyObject& object, TrayClassName className) {
    if (!className) className = ClassName;
    auto current = object;
    for (unsigned depth = 0; current && depth < 32; ++depth) {
        const auto type = className(current);
        if (type == L"SystemTray.SystemTrayFrame") return current.try_as<xaml::FrameworkElement>();
        if (type == L"SystemTray.NotificationAreaOverflow") return nullptr;
        current = media::VisualTreeHelper::GetParent(current);
    }
    return nullptr;
}
bool IsJellyflowerTip(std::wstring_view name) noexcept {
    if (name.empty()) return false;
    for (unsigned language = 0; language < static_cast<unsigned>(Language::Count); ++language)
        if (name == Tr(TextId::TrayTip, static_cast<Language>(language))) return true;
    return false;
}
struct TrayOnlyView::Impl {
    xaml::FrameworkElement frame;
    TrayClassName className;
    std::vector<std::unique_ptr<Part>> parts;
    bool found = false;
    HRESULT error = S_OK;
    Impl(const xaml::FrameworkElement& value, TrayClassName name) : frame(value), className(name ? name : ClassName) {
        if (!frame) winrt::throw_hresult(E_INVALIDARG);
    }
    ~Impl() { Restore(); }
    // Known parts only: an unfamiliar layout keeps everything we do not recognize.
    std::vector<xaml::FrameworkElement> Collect() {
        std::vector<xaml::FrameworkElement> targets, icons;
        xaml::FrameworkElement chevron{nullptr};
        bool flower = false;
        std::vector<xaml::DependencyObject> pending{frame};
        for (unsigned visited = 0; !pending.empty() && visited < MaxVisited; ++visited) {
            const auto item = pending.back(); pending.pop_back();
            if (item != frame) {
                if (const auto element = item.try_as<xaml::FrameworkElement>()) {
                    const auto type = className(item);
                    if (type == L"SystemTray.NotifyIconView") {
                        if (IsJellyflowerTip(IconName(element))) flower = true;
                        else icons.push_back(element);
                        continue;
                    }
                    if (type == L"SystemTray.Stack") {
                        const auto name = element.Name();
                        if (name == L"NotifyIconStack") { chevron = element; continue; }
                        if (name == L"MainStack" || name == L"NonActivatableStack") { targets.push_back(element); continue; }
                    } else if (type == L"SystemTray.OmniButton") {
                        const auto name = element.Name();
                        if (name == L"ControlCenterButton" || name == L"NotificationCenterButton") { targets.push_back(element); continue; }
                    }
                }
            }
            const int count = media::VisualTreeHelper::GetChildrenCount(item);
            if (count > MaxChildren) continue;
            for (int i = count - 1; i >= 0; --i) pending.push_back(media::VisualTreeHelper::GetChild(item, i));
        }
        targets.insert(targets.end(), icons.begin(), icons.end());
        // Without our own icon on this taskbar, the chevron is the way to the menu.
        if (flower && chevron) targets.push_back(chevron);
        found = flower;
        return targets;
    }
    void Release(Part& part) noexcept {
        try { part.Restore(); } catch (...) { error = winrt::to_hresult(); }
    }
    void Refresh(bool enabled) {
        error = S_OK;
        if (!enabled) { Restore(); return; }
        const auto targets = Collect();
        for (auto it = parts.begin(); it != parts.end();) {
            if (std::find(targets.begin(), targets.end(), (*it)->element) != targets.end()) { ++it; continue; }
            Release(**it);
            it = parts.erase(it);
        }
        for (const auto& target : targets) {
            const auto existing = std::find_if(parts.begin(), parts.end(), [&](const auto& part) { return part->element == target; });
            try {
                if (existing != parts.end()) (*existing)->Ensure();
                else parts.push_back(std::make_unique<Part>(target));
            } catch (...) { error = winrt::to_hresult(); }
        }
    }
    void Restore() noexcept {
        for (const auto& part : parts) Release(*part);
        parts.clear();
        found = false;
    }
};
TrayOnlyView::TrayOnlyView(const xaml::FrameworkElement& frame, TrayClassName className) : impl(std::make_unique<Impl>(frame, className)) {}
TrayOnlyView::~TrayOnlyView() = default;
void TrayOnlyView::Refresh(bool enabled) { impl->Refresh(enabled); }
void TrayOnlyView::Restore() noexcept { impl->Restore(); }
unsigned TrayOnlyView::Hidden() const noexcept { return static_cast<unsigned>(impl->parts.size()); }
bool TrayOnlyView::Found() const noexcept { return impl->found; }
HRESULT TrayOnlyView::Error() const noexcept { return impl->error; }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.hpp"
#include "network_art.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <vector>
#include <iterator>

namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace {
winrt::hstring ClassName(const xaml::DependencyObject& object) { return winrt::get_class_name(object); }
bool NetworkSlot(const controls::Grid& grid, XamlClassName name) {
    struct Part { const wchar_t* type; };
    constexpr Part path[]{{L"Windows.UI.Xaml.Controls.Grid"}, {L"SystemTray.IconView"},
        {L"Windows.UI.Xaml.Controls.ContentPresenter"}, {L"Windows.UI.Xaml.Controls.StackPanel"},
        {L"Windows.UI.Xaml.Controls.ItemsPresenter"}, {L"Windows.UI.Xaml.Controls.ContentPresenter"},
        {L"Windows.UI.Xaml.Controls.Grid"}, {L"SystemTray.OmniButton"}};
    xaml::DependencyObject current = grid, presenter{nullptr};
    for (size_t i=0; i<std::size(path); ++i) {
        current = media::VisualTreeHelper::GetParent(current);
        if (!current || name(current) != path[i].type) return false;
        if (i==2) presenter=current;
        if (i==3 && (media::VisualTreeHelper::GetChildrenCount(current)<2
            || media::VisualTreeHelper::GetChild(current,0)!=presenter)) return false;
    }
    const auto root=current.try_as<xaml::FrameworkElement>();
    return root && root.Name()==L"ControlCenterButton";
}
controls::TextBlock FindGlyph(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> pending{root};
    for(unsigned visited=0; !pending.empty() && visited<64; ++visited) {
        auto item=pending.back(); pending.pop_back();
        if(const auto glyph=item.try_as<controls::TextBlock>(); glyph && glyph.Name()==L"InnerTextBlock") return glyph;
        const int count=media::VisualTreeHelper::GetChildrenCount(item);
        if(count>64) return nullptr;
        for(int i=0;i<count;++i) pending.push_back(media::VisualTreeHelper::GetChild(item,i));
    }
    return nullptr;
}
}
bool ConnectedNetworkGlyph(const winrt::hstring& value) noexcept {
    if(value.size()!=1) return false;
    // Deliberately exclude globe, warning/error, disconnected and connecting
    // glyphs. The shell's status remains authoritative; no connectivity polling.
    switch(value[0]) {
    case 0xE701: case 0xE872: case 0xE873: case 0xE874: // Wi-Fi / signal levels
    case 0xE839: case 0xE968: // Ethernet / network
    case 0xF385: case 0xF386: return true; // newer connected variants
    default: return false;
    }
}
NetworkTarget FindNetworkTarget(const xaml::DependencyObject& object, XamlClassName className) {
    if(!className) className=ClassName;
    const auto root=FindVolumeRoot(object,className);
    if(!root) return {};
    std::vector<xaml::DependencyObject> pending{root};
    for(unsigned visited=0; !pending.empty() && visited<256; ++visited) {
        auto item=pending.back(); pending.pop_back();
        if(const auto grid=item.try_as<controls::Grid>(); grid && NetworkSlot(grid,className)) {
            if(const auto glyph=FindGlyph(grid)) return {grid,glyph};
        }
        const int count=media::VisualTreeHelper::GetChildrenCount(item);
        if(count>64) return {};
        for(int i=0;i<count;++i) pending.push_back(media::VisualTreeHelper::GetChild(item,i));
    }
    return {};
}
struct NetworkView::Impl {
    NetworkTarget target;
    controls::Viewbox artwork{nullptr};
    winrt::Windows::Foundation::IInspectable original{nullptr};
    xaml::Data::BindingBase binding{nullptr};
    int64_t textToken=0, opacityToken=0;
    bool enabled=false, applied=false, updating=false;
    explicit Impl(const NetworkTarget& value):target(value) {
        if(!target) winrt::throw_hresult(E_INVALIDARG);
        const std::wstring markup=L"<Path xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Width='97.2493' Height='95.69162' Data='"
            +std::wstring(NetworkPath)+L"'/>";
        const auto path=xaml::Markup::XamlReader::Load(markup).as<xaml::Shapes::Path>();
        xaml::Data::Binding ink;ink.Source(target.glyph);ink.Path(xaml::PropertyPath(L"Foreground"));
        path.SetBinding(xaml::Shapes::Shape::FillProperty(),ink);
        artwork=controls::Viewbox();artwork.Child(path);artwork.Width(16);artwork.Height(16);
        artwork.Stretch(media::Stretch::Uniform);
        artwork.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        artwork.VerticalAlignment(xaml::VerticalAlignment::Center);
        artwork.IsHitTestVisible(false);artwork.Visibility(xaml::Visibility::Collapsed);
        xaml::Automation::AutomationProperties::SetAccessibilityView(artwork,xaml::Automation::Peers::AccessibilityView::Raw);
        textToken=target.glyph.RegisterPropertyChangedCallback(controls::TextBlock::TextProperty(),[this](auto&&,auto&&){Changed(false);});
        try { opacityToken=target.glyph.RegisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(),[this](auto&&,auto&&){Changed(true);}); }
        catch(...) {target.glyph.UnregisterPropertyChangedCallback(controls::TextBlock::TextProperty(),textToken);throw;}
    }
    void Save() {
        const auto property=xaml::UIElement::OpacityProperty();
        original=target.glyph.ReadLocalValue(property);
        const auto expression=target.glyph.GetBindingExpression(property);
        binding=expression?expression.ParentBinding():nullptr;
        artwork.Opacity(target.glyph.Opacity());
    }
    void Changed(bool opacity) noexcept {
        if(updating) return;
        try {
            if(opacity && applied && target.glyph.Opacity()!=0) Save();
            Refresh(enabled);
        } catch(...) {try{Restore();}catch(...) {}}
    }
    void Refresh(bool value) {
        enabled=value;
        if(!enabled || !ConnectedNetworkGlyph(target.glyph.Text())) {Restore();return;}
        updating=true;
        try {
            if(!applied) { Save();applied=true; }
            uint32_t index=0;
            if(!target.slot.Children().IndexOf(artwork,index)) target.slot.Children().Append(artwork);
            // Keep the original glyph, tooltip, accessibility and click target.
            // Opacity preserves its measured slot and all native status bindings.
            target.glyph.Opacity(0);artwork.Visibility(xaml::Visibility::Visible);
            updating=false;
        } catch(...) {updating=false;Restore();throw;}
    }
    void Restore() {
        if(!applied) return;
        updating=true;
        try {
            artwork.Visibility(xaml::Visibility::Collapsed);
            uint32_t index=0;
            if(target.slot.Children().IndexOf(artwork,index)) target.slot.Children().RemoveAt(index);
            if(target.glyph.Opacity()==0) {
                const auto property=xaml::UIElement::OpacityProperty();target.glyph.ClearValue(property);
                if(binding) target.glyph.SetBinding(property,binding);
                else if(original!=xaml::DependencyProperty::UnsetValue()) target.glyph.SetValue(property,original);
            }
            applied=false;updating=false;
        } catch(...) {updating=false;throw;}
    }
    ~Impl() {
        try{target.glyph.UnregisterPropertyChangedCallback(controls::TextBlock::TextProperty(),textToken);}catch(...){}
        try{target.glyph.UnregisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(),opacityToken);}catch(...){}
        try{Restore();}catch(...){}
    }
};
NetworkView::NetworkView(const NetworkTarget& target):impl(std::make_unique<Impl>(target)){}
NetworkView::~NetworkView()=default;
void NetworkView::Refresh(bool enabled){impl->Refresh(enabled);}
void NetworkView::Restore(){impl->Restore();}
bool NetworkView::Applied() const noexcept{return impl->applied;}
}

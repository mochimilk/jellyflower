// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "search_art.hpp"
#include "start_metrics.hpp"
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <cmath>
#include <limits>
namespace acrylic {
inline bool IsSearchInput(const sx::Controls::Control& control,StartClassName name=nullptr) {
    if(!control)return false;
    const auto type=name?name(control):winrt::get_class_name(control);
    return type==L"Cortana.UI.Views.RichSearchBoxControl" || type==L"Cortana.UI.Views.RichComposerBoxControl";
}
// SearchHost owns the real editable field. Style its native template, never
// replace its editor, text/document, selection, bindings or keyboard handlers.
class SearchInput {
    sx::Controls::Control control_;
    StartClassName name_;
    sx::Controls::Grid root_{nullptr},pane_{nullptr};
    sx::FrameworkElement viewport_{nullptr};
    sx::FrameworkElement editor_{nullptr};
    sx::Controls::TextBlock placeholder_{nullptr};
    sx::Controls::Control textBox_{nullptr};
    sx::Controls::Canvas icon_{nullptr},iconLayer_{nullptr},dividerLayer_{nullptr};
    sx::Controls::Border divider_{nullptr};
    StartSearchValues values_;
    winrt::event_token layout_{};
    bool applied_=false,positioning_=false,light_=false;
    sx::FrameworkElement Class(const sx::DependencyObject& from,std::wstring_view type) {
        std::vector<sx::DependencyObject> pending{from};
        for(size_t i=0;i<pending.size() && i<256;++i){
            if((name_?name_(pending[i]):winrt::get_class_name(pending[i]))==type)return pending[i].try_as<sx::FrameworkElement>();
            for(int c=0;c<sx::Media::VisualTreeHelper::GetChildrenCount(pending[i]) && pending.size()<256;++c)pending.push_back(sx::Media::VisualTreeHelper::GetChild(pending[i],c));
        }
        return nullptr;
    }
    static sx::Controls::Canvas Layer(const wchar_t* name) {
        sx::Controls::Canvas layer;layer.Name(name);layer.Width(0);layer.Height(0);
        layer.HorizontalAlignment(sx::HorizontalAlignment::Left);layer.VerticalAlignment(sx::VerticalAlignment::Top);layer.IsHitTestVisible(false);
        sx::Automation::AutomationProperties::SetAccessibilityView(layer,sx::Automation::Peers::AccessibilityView::Raw);
        sx::Controls::Grid::SetRowSpan(layer,100);sx::Controls::Grid::SetColumnSpan(layer,100);return layer;
    }
    static void Remove(const sx::Controls::Grid& parent,const sx::UIElement& child) noexcept {
        try{uint32_t index=0;if(parent && child && parent.Children().IndexOf(child,index))parent.Children().RemoveAt(index);}catch(...){}
    }
    bool Discover(){
        if(!IsSearchInput(control_,name_) || sx::Media::VisualTreeHelper::GetChildrenCount(control_)!=1)return false;
        auto root=sx::Media::VisualTreeHelper::GetChild(control_,0).try_as<sx::Controls::Grid>();if(!root)return false;
        auto text=FindStartElement(root,L"SearchTextBox").try_as<sx::Controls::Control>();
        if(!text)text=Class(root,L"Cortana.UI.Views.CortanaRichSearchBox").try_as<sx::Controls::Control>();
        if(!text)return false;
        auto placeholder=FindStartElement(text,L"PlaceholderTextContentPresenter").try_as<sx::Controls::TextBlock>();
        auto editor=Class(text,L"Windows.UI.Xaml.Controls.ScrollContentPresenter");
        if(!placeholder || !editor)return false;
        sx::Controls::Grid pane{nullptr};auto ancestor=sx::Media::VisualTreeHelper::GetParent(control_);
        for(unsigned i=0;ancestor && i<32;++i){
            if((name_?name_(ancestor):winrt::get_class_name(ancestor))==L"Cortana.UI.Views.TaskbarSearchPage"){
                if(sx::Media::VisualTreeHelper::GetChildrenCount(ancestor)==1)pane=sx::Media::VisualTreeHelper::GetChild(ancestor,0).try_as<sx::Controls::Grid>();break;
            }
            ancestor=sx::Media::VisualTreeHelper::GetParent(ancestor);
        }
        if(!pane)return false;
        if(applied_ && root==root_ && pane==pane_ && editor==editor_ && placeholder==placeholder_)return true;
        Restore();root_=root;pane_=pane;textBox_=text;placeholder_=placeholder;editor_=editor;
        viewport_=FindStartElement(pane,L"LayerBorder");if(!viewport_)viewport_=pane;return true;
    }
    void Ink(){
        auto brush=textBox_.Foreground();
        if(!brush)brush=sx::Media::SolidColorBrush(light_?winrt::Windows::UI::Color{255,0,0,0}:winrt::Windows::UI::Color{255,255,255,255});
        for(auto child:icon_.Children())if(auto shape=child.try_as<sx::Shapes::Shape>())shape.Stroke(brush);
        const auto color=MenuBorder(light_);divider_.Background(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,GetRValue(color),GetGValue(color),GetBValue(color)}));
    }
    void Position()noexcept{
        if(!applied_ || positioning_ || pane_.ActualWidth()<=0 || control_.ActualWidth()<=0)return;positioning_=true;
        try{
            auto p=control_.TransformToVisual(pane_).TransformPoint({0,0});auto margin=control_.Margin();
            const auto viewport=viewport_.TransformToVisual(pane_).TransformPoint({0,0});
            const double left=viewport.X+StartMetrics::SectionInset;
            const double dl=left-p.X,dr=p.X+control_.ActualWidth()-(viewport.X+viewport_.ActualWidth()-StartMetrics::SectionInset);
            if(std::abs(dl)>.25 || std::abs(dr)>.25){margin.Left+=dl;margin.Right+=dr;values_.Update(control_,sx::FrameworkElement::MarginProperty(),winrt::box_value(margin));}
            const auto iconOrigin=iconLayer_.TransformToVisual(pane_).TransformPoint({0,0});
            const double iconLeft=left-iconOrigin.X,iconTop=p.Y+(control_.ActualHeight()-StartMetrics::SearchIcon)/2-iconOrigin.Y;
            if(sx::Controls::Canvas::GetLeft(icon_)!=iconLeft)sx::Controls::Canvas::SetLeft(icon_,iconLeft);
            if(sx::Controls::Canvas::GetTop(icon_)!=iconTop)sx::Controls::Canvas::SetTop(icon_,iconTop);
            if(divider_.Width()!=viewport_.ActualWidth())divider_.Width(viewport_.ActualWidth());
            const auto dividerOrigin=dividerLayer_.TransformToVisual(pane_).TransformPoint({0,0});
            const double dividerLeft=viewport.X-dividerOrigin.X;
            if(sx::Controls::Canvas::GetLeft(divider_)!=dividerLeft)sx::Controls::Canvas::SetLeft(divider_,dividerLeft);
            const double bottom=p.Y+control_.ActualHeight()+margin.Bottom-1-dividerOrigin.Y;
            if(sx::Controls::Canvas::GetTop(divider_)!=bottom)sx::Controls::Canvas::SetTop(divider_,bottom);
        }catch(...){}positioning_=false;
    }
    void ClearGrid(const sx::Controls::Grid& grid,const sx::Media::Brush& clear){
        values_.Set(grid,sx::Controls::Panel::BackgroundProperty(),clear);values_.Set(grid,sx::Controls::Grid::BorderBrushProperty(),clear);
        values_.Set(grid,sx::Controls::Grid::BorderThicknessProperty(),winrt::box_value(sx::Thickness{}));
    }
public:
    explicit SearchInput(const sx::Controls::Control& control,StartClassName name=nullptr):control_(control),name_(name){}
    SearchInput(const SearchInput&)=delete;SearchInput& operator=(const SearchInput&)=delete;
    ~SearchInput(){Restore();}
    void Refresh(bool enabled,bool light){
        if(!enabled){Restore();return;}if(!Discover()){if(applied_)Restore();return;}light_=light;
        if(!applied_)try{
            auto clear=sx::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0});
            values_.Set(control_,sx::Controls::Control::BackgroundProperty(),clear);values_.Set(control_,sx::Controls::Control::BorderBrushProperty(),clear);
            values_.Set(control_,sx::Controls::Control::BorderThicknessProperty(),winrt::box_value(sx::Thickness{}));
            values_.Set(control_,sx::FrameworkElement::WidthProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
            values_.Set(control_,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));values_.Set(control_,sx::FrameworkElement::MaxWidthProperty(),winrt::box_value(std::numeric_limits<double>::infinity()));
            values_.Set(control_,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
            values_.Set(control_,sx::FrameworkElement::MarginProperty(),nullptr,[](const auto& value){auto m=winrt::unbox_value<sx::Thickness>(value);m.Bottom=16;return winrt::box_value(m);});
            // Search is a separate native page. Round all of its painted frame
            // layers together, while leaving Windows' compositor clips intact.
            for(const auto id:{L"AppBorder",L"AccentAppBorder",L"LayerBorder",L"AccentLayerBorder",L"dropshadow",L"HCBorder"})
                if(auto border=FindStartElement(pane_,id).try_as<sx::Controls::Border>()) {
                    const double radius=(id==std::wstring_view(L"LayerBorder") || id==std::wstring_view(L"AccentLayerBorder"))?StartMetrics::SurfaceRadius-1:StartMetrics::SurfaceRadius;
                    values_.Set(border,sx::Controls::Border::CornerRadiusProperty(),winrt::box_value(sx::CornerRadius{radius,radius,radius,radius}));
                }
            // Keep the divider at the end of the search header, then inset the
            // native results region so its web-hosted tabs have breathing room.
            if(auto results=FindStartElement(pane_,L"QueryFormulationRoot"))
                values_.Set(results,sx::FrameworkElement::MarginProperty(),nullptr,[](const auto& value){auto m=winrt::unbox_value<sx::Thickness>(value);m.Top=StartMetrics::SearchResultsGap;return winrt::box_value(m);});
            ClearGrid(root_,clear);values_.Set(textBox_,sx::Controls::Control::BackgroundProperty(),clear);
            values_.Set(textBox_,sx::Controls::Control::BorderBrushProperty(),clear);values_.Set(textBox_,sx::Controls::Control::BorderThicknessProperty(),winrt::box_value(sx::Thickness{}));
            if(sx::Media::VisualTreeHelper::GetChildrenCount(textBox_)==1)if(auto grid=sx::Media::VisualTreeHelper::GetChild(textBox_,0).try_as<sx::Controls::Grid>())ClearGrid(grid,clear);
            for(auto from:{pane_.as<sx::DependencyObject>(),textBox_.as<sx::DependencyObject>()})if(auto b=FindStartElement(from,from==pane_?L"TaskbarSearchBackground":L"BorderElement").try_as<sx::Controls::Border>()){
                values_.Set(b,sx::Controls::Border::BackgroundProperty(),clear);values_.Set(b,sx::Controls::Border::BorderBrushProperty(),clear);values_.Set(b,sx::Controls::Border::BorderThicknessProperty(),winrt::box_value(sx::Thickness{}));
            }
            for(auto name:{L"SearchIconPlayer",L"SearchGlyphContainer",L"SearchBoxOnTaskbarSearchGlyph",L"UnderlineContainer"})if(auto element=FindStartElement(root_,name))values_.Set(element,sx::UIElement::OpacityProperty(),winrt::box_value(0.0));
            // Native template bindings carry this padding into both the
            // editable scroll viewport/caret and placeholder; no render shifts.
            values_.Set(textBox_,sx::Controls::Control::PaddingProperty(),nullptr,[](const auto& value){auto p=winrt::unbox_value<sx::Thickness>(value);p.Left=StartMetrics::SearchIcon+StartMetrics::SearchGap;return winrt::box_value(p);});
            // Preserve the template's placeholder binding. Its source lives
            // on the native editor, including the RichEditBox variant.
            if(textBox_.try_as<sx::Controls::TextBox>())values_.Set(textBox_,sx::Controls::TextBox::PlaceholderTextProperty(),winrt::box_value(L"search"));
            else if(textBox_.try_as<sx::Controls::RichEditBox>())values_.Set(textBox_,sx::Controls::RichEditBox::PlaceholderTextProperty(),winrt::box_value(L"search"));
            icon_=SearchMagnifier();iconLayer_=Layer(L"JellyflowerResultsSearchIcon");iconLayer_.Children().Append(icon_);
            dividerLayer_=Layer(L"JellyflowerResultsSearchDivider");divider_=sx::Controls::Border();divider_.Height(1);dividerLayer_.Children().Append(divider_);
            root_.Children().Append(iconLayer_);pane_.Children().Append(dividerLayer_);applied_=true;layout_=control_.LayoutUpdated([this](auto&&,auto&&){Position();});
        }catch(...){Restore();throw;}
        Ink();Position();
    }
    void Restore()noexcept{
        applied_=false;if(layout_.value){try{control_.LayoutUpdated(layout_);}catch(...){}layout_={};}
        Remove(root_,iconLayer_);Remove(pane_,dividerLayer_);values_.Clear();icon_=nullptr;iconLayer_=nullptr;dividerLayer_=nullptr;divider_=nullptr;
    }
    bool Applied()const noexcept{return applied_;}
};
}

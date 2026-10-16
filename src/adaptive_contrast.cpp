// SPDX-License-Identifier: GPL-3.0-or-later
#include "adaptive_contrast.hpp"
#include "adaptive_material.hpp"
#include "adaptive_bitmap.hpp"
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <vector>
#include <cmath>

namespace acrylic {
namespace x=winrt::Windows::UI::Xaml;
namespace controls=x::Controls;
namespace media=x::Media;
namespace {
winrt::hstring NativeName(x::DependencyObject const& object){return winrt::get_class_name(object);}
bool Neutral(media::Brush const& brush){
    if(!brush)return true;
    const auto solid=brush.try_as<media::SolidColorBrush>();if(!solid)return false;
    const auto c=solid.Color();return std::max({c.R,c.G,c.B})-std::min({c.R,c.G,c.B})<=2;
}
struct Property {
    x::FrameworkElement element{nullptr};x::DependencyProperty key{nullptr};
    winrt::Windows::Foundation::IInspectable local{nullptr};x::Data::BindingBase binding{nullptr};
    media::Brush native{nullptr},ink{nullptr};
    Property(x::FrameworkElement const& e,x::DependencyProperty const& k):element(e),key(k){}
    media::Brush Current() const{return element.GetValue(key).try_as<media::Brush>();}
    void Save(){local=element.ReadLocalValue(key);const auto expression=element.GetBindingExpression(key);binding=expression?expression.ParentBinding():nullptr;native=Current();}
    void Restore(){if(!ink || Current()!=ink)return;element.ClearValue(key);if(binding)element.SetBinding(key,binding);else if(local!=x::DependencyProperty::UnsetValue())element.SetValue(key,local);}
};
bool Visible(x::FrameworkElement const& element){
    if(!element.IsLoaded() || !element.XamlRoot() || !element.XamlRoot().IsHostVisible() || element.ActualWidth()<=0 || element.ActualHeight()<=0)return false;
    for(x::DependencyObject current=element;current;current=media::VisualTreeHelper::GetParent(current)){
        const auto e=current.try_as<x::UIElement>();if(e && e.Visibility()!=x::Visibility::Visible)return false;
    }
    return true;
}
}
bool IsAdaptiveTrayTarget(x::DependencyObject const& object,AdaptiveClassName name){
    if(!name)name=NativeName;
    const auto element=object.try_as<x::FrameworkElement>();if(!element)return false;
    const auto ownName=element.Name();
    if(ownName==L"AcrylicTaskbarClockLine" || ownName==L"DateInnerTextBlock" || ownName==L"TimeInnerTextBlock" || ownName==L"JellyflowerAdaptiveMask")return false;
    if(!object.try_as<controls::TextBlock>() && !object.try_as<controls::IconElement>() && !object.try_as<x::Shapes::Path>() && !object.try_as<controls::Image>())return false;
    bool frame=false;
    auto parent=media::VisualTreeHelper::GetParent(object);
    for(unsigned depth=0;parent && depth<48;++depth,parent=media::VisualTreeHelper::GetParent(parent)){
        const auto type=name(parent);
        if(type==L"SystemTray.NotificationAreaOverflow")return false;
        if(type==L"SystemTray.SystemTrayFrame")frame=true;
        // Prefer an IconElement's foreground over modifying its template too.
        if(parent.try_as<controls::IconElement>())return false;
    }
    return frame;
}
struct AdaptiveView::Impl : std::enable_shared_from_this<Impl> {
    x::FrameworkElement target;
    controls::Image image{nullptr};
    std::vector<Property> properties;
    std::vector<int64_t> tokens;
    struct BrushWatch{media::SolidColorBrush brush{nullptr};int64_t color=0,opacity=0;bool hasColor=false,hasOpacity=false;};
    std::vector<BrushWatch> brushWatches;
    bool(*ownerAlive)()=nullptr;
    bool enabled=false,visible=false,applied=false,updating=false,alive=true;
    HRESULT error=S_OK;
    AdaptiveStats stats;
    uint64_t lastRead=0,hash=0,generation=0,retryAfter=0;
    bool inspected=false,dirty=true,loaded=false,sampled=false;
    media::ImageSource source{nullptr};
    media::LoadedImageSurface surface{nullptr};
    winrt::Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
    winrt::event_token loadToken{},openToken{},sizeToken{};
    bool loadRegistered=false,sourceRegistered=false,opacityRegistered=false,openRegistered=false,sizeRegistered=false;
    controls::Panel panel{nullptr};controls::Canvas overlay{nullptr};x::Shapes::Rectangle mask{nullptr};
    winrt::Windows::Foundation::IInspectable opacityLocal{nullptr};x::Data::BindingBase opacityBinding{nullptr};
    double nativeOpacity=1;
    int64_t sourceToken=0,opacityToken=0;
    explicit Impl(x::DependencyObject const& object,bool(*owner)()):target(object.as<x::FrameworkElement>()),image(object.try_as<controls::Image>()),ownerAlive(owner){
        if(image)return;
        if(object.try_as<controls::TextBlock>())properties.emplace_back(target,controls::TextBlock::ForegroundProperty());
        else if(object.try_as<controls::IconElement>())properties.emplace_back(target,controls::IconElement::ForegroundProperty());
        else if(object.try_as<x::Shapes::Path>()){
            properties.emplace_back(target,x::Shapes::Shape::FillProperty());properties.emplace_back(target,x::Shapes::Shape::StrokeProperty());
        }else winrt::throw_hresult(E_INVALIDARG);
    }
    void Connect(){
        const std::weak_ptr<Impl> weak=shared_from_this();
        if(image){
            sourceToken=image.RegisterPropertyChangedCallback(controls::Image::SourceProperty(),[weak](auto&&,auto&&){if(auto self=weak.lock())self->ImageChanged();});sourceRegistered=true;
            opacityToken=image.RegisterPropertyChangedCallback(x::UIElement::OpacityProperty(),[weak](auto&&,auto&&){if(auto self=weak.lock())self->OpacityChanged();});opacityRegistered=true;
            openToken=image.ImageOpened([weak](auto&&,auto&&){if(auto self=weak.lock())self->ImageChanged();});openRegistered=true;
            sizeToken=image.SizeChanged([weak](auto&&,auto&&){if(auto self=weak.lock())if(!self->Geometry())try{self->RemoveOverlay();}catch(...){}});sizeRegistered=true;
        }else {tokens.reserve(properties.size());for(size_t i=0;i<properties.size();++i){
            tokens.push_back(target.RegisterPropertyChangedCallback(properties[i].key,[weak,i](auto&&,auto&&){if(auto self=weak.lock())self->Changed(i);}));
        }}
    }
    bool Allowed()const{return alive && enabled && (!ownerAlive || ownerAlive());}
    void WatchBrushes(){
        for(auto& watch:brushWatches){if(watch.hasColor)watch.brush.UnregisterPropertyChangedCallback(media::SolidColorBrush::ColorProperty(),watch.color);if(watch.hasOpacity)watch.brush.UnregisterPropertyChangedCallback(media::Brush::OpacityProperty(),watch.opacity);}
        brushWatches.clear();const std::weak_ptr<Impl> weak=shared_from_this();
        for(auto const& property:properties)if(auto brush=property.native.try_as<media::SolidColorBrush>()){
            brushWatches.push_back({});auto& watch=brushWatches.back();watch.brush=brush;
            auto changed=[weak](auto&&,auto&&){if(auto self=weak.lock())self->Refresh(self->enabled,self->visible,GetTickCount64());};
            watch.color=brush.RegisterPropertyChangedCallback(media::SolidColorBrush::ColorProperty(),changed);watch.hasColor=true;
            watch.opacity=brush.RegisterPropertyChangedCallback(media::Brush::OpacityProperty(),changed);watch.hasOpacity=true;
        }
    }
    void Changed(size_t index)noexcept{
        if(updating || !alive)return;
        try{if(applied && properties[index].Current()!=properties[index].ink){properties[index].Save();WatchBrushes();}Refresh(enabled,visible,GetTickCount64());}
        catch(...){error=winrt::to_hresult();try{Restore();}catch(...){}}
    }
    void Glyph(){
        if(!Allowed()){RestoreGlyph();return;}
        if(!applied){for(auto& p:properties)p.Save();WatchBrushes();}
        // SetValue replaces a XAML binding. Do not sever a live status-color
        // binding: future colored warning states must continue to arrive.
        bool any=false;for(auto const& p:properties){if(p.binding || !Neutral(p.native)){RestoreGlyph();return;}any|=bool(p.native);}
        if(!any)return;
        updating=true;
        try{
            for(auto& p:properties)if(auto solid=p.native.try_as<media::SolidColorBrush>()){
                const auto c=solid.Color();if(!p.ink)p.ink=winrt::make<adaptive::Ink>(target,c);
                auto brush=p.ink.as<media::XamlCompositionBrushBase>();const winrt::Windows::UI::Color fallback{255,c.R,c.G,c.B};
                if(brush.FallbackColor()!=fallback)brush.FallbackColor(fallback);
                const double opacity=solid.Opacity()*c.A/255.;if(p.ink.Opacity()!=opacity)p.ink.Opacity(opacity);
                if(p.Current()!=p.ink)target.SetValue(p.key,p.ink);
            }
            applied=true;updating=false;
        }catch(...){updating=false;RestoreGlyph();throw;}
    }
    void RestoreGlyph(){
        updating=true;try{for(auto& p:properties){p.Restore();p.ink=nullptr;}applied=false;updating=false;}catch(...){updating=false;throw;}
    }
    void SaveOpacity(){opacityLocal=image.ReadLocalValue(x::UIElement::OpacityProperty());const auto expression=image.GetBindingExpression(x::UIElement::OpacityProperty());opacityBinding=expression?expression.ParentBinding():nullptr;nativeOpacity=image.Opacity();}
    void OpacityChanged()noexcept{
        if(updating || !alive || !applied)return;
        try{SaveOpacity();if(opacityBinding){RemoveOverlay();ClearSurface();dirty=true;return;}if(nativeOpacity==0)return;
            if(overlay)overlay.Opacity(nativeOpacity);updating=true;image.Opacity(0);updating=false;
        }catch(...){updating=false;error=winrt::to_hresult();}
    }
    void RemoveOverlay(){
        updating=true;
        try{
            if(applied && image.Opacity()==0){image.ClearValue(x::UIElement::OpacityProperty());if(opacityBinding)image.SetBinding(x::UIElement::OpacityProperty(),opacityBinding);else if(opacityLocal!=x::DependencyProperty::UnsetValue())image.SetValue(x::UIElement::OpacityProperty(),opacityLocal);}
            if(panel && overlay){uint32_t index{};if(panel.Children().IndexOf(overlay,index))panel.Children().RemoveAt(index);}
            mask=nullptr;overlay=nullptr;panel=nullptr;applied=false;updating=false;
        }catch(...){updating=false;throw;}
    }
    void ClearSurface(){
        ++generation;
        if(surface){if(loadRegistered)surface.LoadCompleted(loadToken);loadRegistered=false;surface.Close();surface=nullptr;}
        stream=nullptr;loaded=false;
    }
    void ImageChanged()noexcept{
        if(!alive || updating)return;
        try{
            // Explorer can replace a notification bitmap or raise ImageOpened
            // for identical artwork. Preserve the mask, overlay position and
            // source opacity across that event; otherwise native/adaptive ink
            // flashes on every NIM_MODIFY. Only tiny CPU-resident bytes are
            // compared, with no new PNG/GPU surface for unchanged artwork.
            const auto nextSource=image.Source();
            if(Allowed() && surface && visible && Visible(image) && !image.GetBindingExpression(x::UIElement::OpacityProperty())){
                if(auto bitmap=nextSource.try_as<media::Imaging::WriteableBitmap>()){
                    ++stats.bitmapReads;const auto next=adaptive::Inspect(bitmap);
                    if(next.monochrome && next.hash==hash){source=nextSource;Geometry();return;}
                }
            }
            if(image.GetBindingExpression(x::UIElement::OpacityProperty()))SaveOpacity();
            RemoveOverlay();ClearSurface();source=nullptr;dirty=true;inspected=false;Refresh(enabled,visible,GetTickCount64());
        }catch(...){error=winrt::to_hresult();try{RemoveOverlay();ClearSurface();}catch(...){}}
    }
    bool Geometry()noexcept{
        if(!applied || !panel || !overlay || !mask)return true;
        try{
            const auto bounds=image.TransformToVisual(overlay).TransformBounds({0,0,static_cast<float>(image.ActualWidth()),static_cast<float>(image.ActualHeight())});
            if(!std::isfinite(bounds.Width) || !std::isfinite(bounds.Height) || bounds.Width<=0 || bounds.Height<=0)return false;
            if(mask.Width()!=bounds.Width)mask.Width(bounds.Width);if(mask.Height()!=bounds.Height)mask.Height(bounds.Height);
            if(controls::Canvas::GetLeft(mask)!=bounds.X)controls::Canvas::SetLeft(mask,bounds.X);
            if(controls::Canvas::GetTop(mask)!=bounds.Y)controls::Canvas::SetTop(mask,bounds.Y);
            if(auto ink=mask.Fill().try_as<media::XamlCompositionBrushBase>())if(auto brush=ink.as<media::IXamlCompositionBrushBaseProtected>().CompositionBrush().try_as<adaptive::composition::CompositionMaskBrush>()){
                auto bitmap=brush.Mask().try_as<adaptive::composition::CompositionSurfaceBrush>();
                if(bitmap)bitmap.Stretch(image.Stretch()==media::Stretch::Fill?adaptive::composition::CompositionStretch::Fill:image.Stretch()==media::Stretch::UniformToFill?adaptive::composition::CompositionStretch::UniformToFill:image.Stretch()==media::Stretch::None?adaptive::composition::CompositionStretch::None:adaptive::composition::CompositionStretch::Uniform);
            }return true;
        }catch(...){error=winrt::to_hresult();return false;}
    }
    void Complete(uint64_t request,media::LoadedImageSourceLoadStatus status)noexcept{
        try{
            if(request!=generation || !alive)return;
            if(loadRegistered){surface.LoadCompleted(loadToken);loadRegistered=false;}
            if(status!=media::LoadedImageSourceLoadStatus::Success){error=E_FAIL;dirty=true;retryAfter=GetTickCount64()+5000;ClearSurface();return;}
            loaded=true;
            if(!Allowed() || !visible || !Visible(image))return;
            auto bitmap=image.Source().try_as<media::Imaging::WriteableBitmap>();if(!bitmap || image.Source()!=source)return;
            ++stats.bitmapReads;const auto current=adaptive::Inspect(bitmap);
            if(!current.monochrome || current.hash!=hash){ImageChanged();return;}
            if(applied)return;
            panel=media::VisualTreeHelper::GetParent(image).try_as<controls::Panel>();if(!panel)return;
            overlay=controls::Canvas();overlay.Width(0);overlay.Height(0);overlay.HorizontalAlignment(x::HorizontalAlignment::Left);overlay.VerticalAlignment(x::VerticalAlignment::Top);overlay.IsHitTestVisible(false);overlay.Opacity(image.Opacity());
            mask=x::Shapes::Rectangle();mask.Name(L"JellyflowerAdaptiveMask");mask.IsHitTestVisible(false);overlay.Children().Append(mask);
            const auto compositor=x::Hosting::ElementCompositionPreview::GetElementVisual(image).Compositor();auto alpha=compositor.CreateSurfaceBrush(surface);alpha.Stretch(adaptive::composition::CompositionStretch::Uniform);
            mask.Fill(winrt::make<adaptive::Ink>(mask,winrt::Windows::UI::Color{255,255,255,255},alpha));
            panel.Children().Append(overlay);SaveOpacity();applied=true;if(!Geometry()){RemoveOverlay();return;}
            updating=true;image.Opacity(0);updating=false;
        }catch(...){updating=false;error=winrt::to_hresult();dirty=true;retryAfter=GetTickCount64()+5000;try{RemoveOverlay();ClearSurface();}catch(...){}}
    }
    void Bitmap(uint64_t now){
        if(!Allowed()){RemoveOverlay();ClearSurface();inspected=false;return;}
        if(image.GetBindingExpression(x::UIElement::OpacityProperty())){SaveOpacity();RemoveOverlay();ClearSurface();inspected=false;return;}
        if(!visible || !Visible(image)){if(overlay)overlay.Visibility(x::Visibility::Collapsed);return;}
        if(overlay)overlay.Visibility(x::Visibility::Visible);
        // Source replacement must not bypass the cap (some tray apps animate
        // by assigning a fresh bitmap on every frame).
        if(sampled && now-lastRead<250){Geometry();return;}
        sampled=true;lastRead=now;const auto nextSource=image.Source();auto bitmap=nextSource.try_as<media::Imaging::WriteableBitmap>();
        if(!bitmap){RemoveOverlay();ClearSurface();source=nextSource;inspected=true;return;}
        ++stats.bitmapReads;const auto next=adaptive::Inspect(bitmap);
        const bool changed=!inspected || nextSource!=source || next.hash!=hash || dirty;
        inspected=true;dirty=false;
        if(changed){
            RemoveOverlay();ClearSurface();source=nextSource;hash=next.hash;
            if(!next.monochrome)return;
            stream=adaptive::MaskStream(next);surface=media::LoadedImageSurface::StartLoadFromStream(stream);++stats.maskBuilds;
            const auto request=generation;const std::weak_ptr<Impl> weak=shared_from_this();
            loadToken=surface.LoadCompleted([weak,request](auto&&,auto&& args){if(auto self=weak.lock())self->Complete(request,args.Status());});loadRegistered=true;
            if(surface.DecodedSize().Width>0)Complete(request,media::LoadedImageSourceLoadStatus::Success);
        }else if(loaded && !applied)Complete(generation,media::LoadedImageSourceLoadStatus::Success);
        Geometry();
    }
    void Refresh(bool active,bool shown,uint64_t now)noexcept{
        enabled=active;visible=shown;
        if(active && now<retryAfter)return;
        error=S_OK;
        try{if(image)Bitmap(now);else Glyph();}catch(...){error=winrt::to_hresult();retryAfter=now+5000;try{Restore();}catch(...){}}
    }
    void Restore(){enabled=false;if(image){RemoveOverlay();ClearSurface();inspected=false;}else RestoreGlyph();}
    void Shutdown()noexcept{
        alive=false;try{
            Restore();
            if(sourceRegistered)image.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(),sourceToken);
            if(opacityRegistered)image.UnregisterPropertyChangedCallback(x::UIElement::OpacityProperty(),opacityToken);
            if(openRegistered)image.ImageOpened(openToken);if(sizeRegistered)image.SizeChanged(sizeToken);
            for(size_t i=0;i<tokens.size();++i)target.UnregisterPropertyChangedCallback(properties[i].key,tokens[i]);tokens.clear();
            for(auto& watch:brushWatches){if(watch.hasColor)watch.brush.UnregisterPropertyChangedCallback(media::SolidColorBrush::ColorProperty(),watch.color);if(watch.hasOpacity)watch.brush.UnregisterPropertyChangedCallback(media::Brush::OpacityProperty(),watch.opacity);}brushWatches.clear();
        }catch(...){}
    }
};
AdaptiveView::AdaptiveView(x::DependencyObject const& object,bool(*owner)()):impl(std::make_shared<Impl>(object,owner)){try{impl->Connect();}catch(...){impl->Shutdown();throw;}}
AdaptiveView::~AdaptiveView(){impl->Shutdown();}
void AdaptiveView::Refresh(bool enabled,bool visible,uint64_t now){impl->Refresh(enabled,visible,now);}
void AdaptiveView::Restore(){impl->Restore();}
bool AdaptiveView::Applied()const{return impl->applied;}
bool AdaptiveView::IsBitmap()const{return impl->image!=nullptr;}
HRESULT AdaptiveView::Error()const{return impl->error;}
AdaptiveStats AdaptiveView::Stats()const{return impl->stats;}
}

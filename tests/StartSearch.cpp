// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_search.hpp"
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
void Pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
void Layout(const sx::FrameworkElement& root){root.UpdateLayout();for(unsigned i=0;i<4;++i){Pump();Sleep(20);}root.UpdateLayout();}
sx::Controls::ControlTemplate Template(){
    return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="ToggleButton"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="44"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Border x:Name="BorderElement" Grid.ColumnSpan="2" Background="#FF343434" BorderBrush="#FF666666" BorderThickness="1" CornerRadius="20"/><Grid x:Name="UnderlineContainer" Grid.ColumnSpan="2"><Border x:Name="BorderUnderline" Height="1" VerticalAlignment="Bottom" Background="White"/></Grid><FontIcon x:Name="SearchGlyph" Glyph="&#xE721;" Opacity="0.8"/><Image x:Name="SearchIconOff" Opacity="0.75"/><Image x:Name="SearchIconOn" Opacity="0.25"/><ContentPresenter x:Name="ContentPresenter" Grid.Column="1" HorizontalAlignment="Left" VerticalAlignment="Center"><TextBlock x:Name="PlaceholderText" Text="Search for apps, settings, and documents" FontSize="14" Foreground="#FFAAAAAA"/></ContentPresenter><Rectangle x:Name="TextCaret" Grid.Column="1" Width="1" Height="18" HorizontalAlignment="Left" Opacity="0"/></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>();
}
winrt::hstring Supported(const sx::DependencyObject&){return L"StartMenu.SearchBoxToggleButton";}
void RenderChecks(const sx::Controls::Grid& pane,const sx::Controls::Canvas& icon,bool light){
    sx::Media::Imaging::RenderTargetBitmap image;
    auto render=image.RenderAsync(pane);
    for(unsigned i=0;i<300 && render.Status()==winrt::Windows::Foundation::AsyncStatus::Started;++i){Pump();Sleep(10);}
    Check(render.Status()==winrt::Windows::Foundation::AsyncStatus::Completed,"XAML renders search fixture");render.GetResults();
    auto read=image.GetPixelsAsync();
    for(unsigned i=0;i<300 && read.Status()==winrt::Windows::Foundation::AsyncStatus::Started;++i){Pump();Sleep(10);}
    Check(read.Status()==winrt::Windows::Foundation::AsyncStatus::Completed,"rendered header pixels available");
    const auto buffer=read.GetResults();const auto* pixels=buffer.data();
    const double scale=image.PixelWidth()/pane.ActualWidth();
    Check(std::abs(image.PixelHeight()/scale-pane.ActualHeight())<1,"opaque fixture background retains the full capture origin");
    const auto point=icon.TransformToVisual(pane).TransformPoint({0,0});unsigned ink=0;
    for(int y=static_cast<int>(point.Y*scale);y<static_cast<int>((point.Y+20)*scale);++y)
        for(int x=static_cast<int>(point.X*scale);x<static_cast<int>((point.X+20)*scale);++x)
            if(light?pixels[(static_cast<size_t>(y)*image.PixelWidth()+x)*4]<128:pixels[(static_cast<size_t>(y)*image.PixelWidth()+x)*4]>128) ++ink;
    Check(ink>30,"Lucide strokes render visibly outside the zero-size overlay container");
    // Fractional-DPI layout rounding can move a one-DIP stroke to either
    // adjacent physical row. Inspect its narrow band, not one pixel center.
    for(const double x:{10.0,pane.ActualWidth()-10}){
        bool found=false;
        for(int y=static_cast<int>(62*scale);y<static_cast<int>(65*scale);++y){
            const auto offset=(static_cast<size_t>(y)*image.PixelWidth()+static_cast<int>(x*scale))*4;
            const int alpha=pixels[offset+3];const int stroke=alpha?pixels[offset]*255/alpha:0;
            found|=alpha>64 && std::abs(stroke-(light?210:69))<3;
        }
        Check(found,"full-width header divider renders with themed stroke");
    }
}
int main(){
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        WNDCLASSW wc{};wc.lpszClassName=L"jellyflower.StartSearchFixture";wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
        const HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower search fixture",WS_POPUP,100,100,1000,600,nullptr,nullptr,wc.hInstance,nullptr);
        sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,1000,600,SWP_SHOWWINDOW);
        sx::Controls::Grid pane;pane.Name(L"MainContent");pane.Width(640);pane.HorizontalAlignment(sx::HorizontalAlignment::Left);
        // RenderTargetBitmap trims transparent outer space. An opaque fixture
        // background preserves the element origin for exact pixel positions.
        pane.Background(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,16,16,16}));
        sx::Controls::Grid header;header.Height(64);header.Margin({32,0,28,0});header.VerticalAlignment(sx::VerticalAlignment::Top);pane.Children().Append(header);
        sx::Controls::Primitives::ToggleButton button;button.Height(32);button.HorizontalAlignment(sx::HorizontalAlignment::Stretch);button.Template(Template());header.Children().Append(button);
        sx::Controls::TextBlock heading;heading.Name(L"PinnedListHeaderText");heading.Text(L"Pinned");heading.Margin({64,80,0,0});heading.HorizontalAlignment(sx::HorizontalAlignment::Left);heading.VerticalAlignment(sx::VerticalAlignment::Top);pane.Children().Append(heading);
        sx::Media::SolidColorBrush primary(winrt::Windows::UI::Color{255,245,245,245});heading.Foreground(primary);
        host.Content(pane);ShowWindow(window,SW_SHOWNOACTIVATE);Layout(pane);
        Check(!IsStartSearch(button) && IsStartSearch(button,Supported),"only the two native Start control classes match the watcher guard");
        const auto originalTemplate=button.Template();const auto nativePlaceholder=FindStartElement(button,L"PlaceholderText").as<sx::Controls::TextBlock>();
        const auto nativeOutline=FindStartElement(button,L"BorderElement").as<sx::Controls::Border>();const auto originalBackground=nativeOutline.Background();
        const auto originalFont=nativePlaceholder.FontFamily().Source();const auto originalSize=nativePlaceholder.FontSize();
        const auto nativePresenter=FindStartElement(button,L"ContentPresenter").as<sx::Controls::ContentPresenter>();const auto originalTransform=nativePresenter.ReadLocalValue(sx::UIElement::RenderTransformProperty());
        bool checked=false;const auto checkToken=button.Checked([&](auto&&,auto&&){checked=true;});
        {
            StartSearch search(button);
            for(bool light:{false,true}){
                primary.Color(light?winrt::Windows::UI::Color{255,0,0,0}:winrt::Windows::UI::Color{255,245,245,245});
                pane.Background(sx::Media::SolidColorBrush(light?winrt::Windows::UI::Color{255,249,249,249}:winrt::Windows::UI::Color{255,16,16,16}));
                search.Refresh(true,light);Layout(pane);search.Refresh(true,light);Layout(pane);
                Check(search.Applied() && button.Template()==originalTemplate && button.IsTabStop(),"native search button/template/keyboard navigation retained");
                Check(nativePlaceholder.Text()==L"search" && sx::Automation::AutomationProperties::GetName(button)==L"search","lowercase visible and accessible search labels");
                Check(nativePlaceholder.FontFamily().Source()==originalFont && nativePlaceholder.FontSize()==originalSize,"native search typography retained");
                Check(nativeOutline.BorderThickness().Left==0 && nativeOutline.Background().as<sx::Media::SolidColorBrush>().Color().A==0,"outlined input fill and border removed");
                for(const auto name:{L"SearchGlyph",L"SearchIconOff",L"SearchIconOn",L"BorderUnderline"})Check(FindStartElement(button,name).Opacity()==0,"native icon variants and input underline hidden");
                const auto layer=FindStartElement(button,L"JellyflowerSearchIcon").as<sx::Controls::Canvas>();Check(!layer.IsHitTestVisible(),"replacement icon cannot intercept native clicks");
                const auto icon=layer.Children().GetAt(0).as<sx::Controls::Canvas>();const auto position=icon.TransformToVisual(pane).TransformPoint({0,0});
                const auto handle=icon.Children().GetAt(0).as<sx::Shapes::Path>();const auto circle=icon.Children().GetAt(1).as<sx::Shapes::Ellipse>();
                const auto bounds=handle.Data().Bounds();
                Check(std::abs(handle.StrokeThickness()-2*20.0/24)<.001 && handle.StrokeStartLineCap()==sx::Media::PenLineCap::Round && handle.StrokeEndLineCap()==sx::Media::PenLineCap::Round
                    && std::abs(bounds.X-16.66*20/24)<.01 && std::abs(bounds.Y-16.66*20/24)<.01 && std::abs(bounds.Width-4.34*20/24)<.01,"Lucide handle scales uniformly to 20 DIP with round caps");
                Check(std::abs(circle.Width()-16*20.0/24)<.001 && circle.Height()==circle.Width() && sx::Controls::Canvas::GetLeft(circle)==2.5 && sx::Controls::Canvas::GetTop(circle)==2.5,"circle retains Lucide proportions at 20 DIP");
                Check(std::abs(position.X-32)<.1 && icon.Width()==20 && icon.Height()==20,"20-DIP canvas aligns with shared section inset");
                Check(handle.Stroke()==primary && circle.Stroke()==primary && nativePlaceholder.Foreground()!=primary,"magnifier uses native primary ink while placeholder stays secondary");
                Check(std::abs(sx::Controls::Canvas::GetTop(icon)-(button.ActualHeight()-20)/2)<.1,"smaller magnifier stays vertically centered");
                const auto lineLayer=FindStartElement(pane,L"JellyflowerSearchDivider").as<sx::Controls::Canvas>();const auto line=lineLayer.Children().GetAt(0).as<sx::Controls::Border>();
                Check(line.Width()==pane.ActualWidth() && line.Height()==1 && std::abs(line.TransformToVisual(pane).TransformPoint({0,0}).Y-63)<.1,"one-DIP stroke spans the entire header below search");
                const auto color=line.Background().as<sx::Media::SolidColorBrush>().Color();Check(color.R==(light?210:69),"divider follows existing translucent-menu border theme");
                RenderChecks(pane,icon,light);
                heading.Margin({80,80,0,0});Layout(pane);Check(std::abs(icon.TransformToVisual(pane).TransformPoint({0,0}).X-32)<.1,"heading layout changes do not move the persistent header anchor");heading.Margin({64,80,0,0});
                sx::Media::TranslateTransform offscreen;offscreen.X(5000);heading.RenderTransform(offscreen);heading.Visibility(sx::Visibility::Collapsed);Layout(pane);
                Check(std::abs(icon.TransformToVisual(pane).TransformPoint({0,0}).X-32)<.1,"Show all's hidden/offscreen home heading cannot displace search");
                const auto showAllText=nativePresenter.TransformToVisual(pane).TransformPoint({0,0});Check(std::abs(showAllText.X-60)<.1,"Show all keeps the placeholder after the magnifier");
                heading.ClearValue(sx::UIElement::RenderTransformProperty());heading.Visibility(sx::Visibility::Visible);
                pane.Width(720);Layout(pane);Check(line.Width()==720,"divider follows native width changes");pane.Width(640);
                const auto text=nativePresenter.TransformToVisual(pane).TransformPoint({0,0});Check(std::abs(text.X-60)<.1,"placeholder has 8 DIP after the 20-DIP icon");
                button.IsChecked(true);Check(checked,"native toggle behavior and event handlers retained");button.IsChecked(false);checked=false;
                search.Refresh(false,light);Layout(pane);
                Check(!search.Applied() && nativePlaceholder.Text()==L"Search for apps, settings, and documents" && nativeOutline.Background()==originalBackground,"disable restores native label and input fill");
                Check(nativePresenter.ReadLocalValue(sx::UIElement::RenderTransformProperty())==originalTransform,"unset original transform is restored");
                Check(!FindStartElement(pane,L"JellyflowerSearchIcon") && !FindStartElement(pane,L"JellyflowerSearchDivider"),"owned visuals removed completely");
            }
            heading.Name(L"UnrecognizedHeading");search.Refresh(true,false);Layout(pane);
            const auto compactIcon=FindStartElement(button,L"JellyflowerSearchIcon").as<sx::Controls::Canvas>().Children().GetAt(0).as<sx::Controls::Canvas>();
            Check(header.Margin().Left==32 && header.Margin().Right==32 && std::abs(compactIcon.TransformToVisual(pane).TransformPoint({0,0}).X-32)<.1,"compact header has symmetric margins and 32-DIP shared alignment");
            search.Restore();heading.Name(L"PinnedListHeaderText");Layout(pane);Check(header.Margin().Left==32 && header.Margin().Right==28,"native asymmetric header margins restore");
            search.Refresh(true,false);nativePlaceholder.Text(L"new stock localized text");Check(nativePlaceholder.Text()==L"search","shell localization changes retain custom placeholder");search.Restore();Check(nativePlaceholder.Text()==L"new stock localized text","restoration uses the latest native text");
            sx::Data::Binding binding;binding.Source(winrt::box_value(L"bound stock text"));nativePlaceholder.SetBinding(sx::Controls::TextBlock::TextProperty(),binding);search.Refresh(true,true);search.Restore();Check(nativePlaceholder.GetBindingExpression(sx::Controls::TextBlock::TextProperty()).ParentBinding()==binding,"original text binding restored");
            search.Refresh(true,false);button.Template(Template());Layout(pane);search.Refresh(true,false);Layout(pane);Check(search.Applied() && FindStartElement(button,L"PlaceholderText").as<sx::Controls::TextBlock>().Text()==L"search","replacement native templates are rediscovered without duplicate overlays");
        }
        Check(!FindStartElement(pane,L"JellyflowerSearchDivider") && !FindStartElement(button,L"JellyflowerSearchIcon"),"destructor restores the native header");
        button.Checked(checkToken);host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();
        std::puts("PASS: native search control behavior, exact Lucide geometry, header divider/alignment, theme/resize changes, typography and local/binding/latest/template/destructor restoration.");return 0;
    }catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx\n",static_cast<unsigned long>(e.code()));}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;
}

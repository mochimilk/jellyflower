// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/search_input.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
const std::wstring& FixtureGreeting(){static const auto text=SearchGreeting(L"Jelly",0);return text;}
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Layout(const sx::FrameworkElement& root){for(unsigned i=0;i<8;++i){root.UpdateLayout();Pump();Sleep(12);}}
winrt::hstring Supported(const sx::DependencyObject& o){
    auto f=o.try_as<sx::FrameworkElement>();if(f && f.Name()==L"Page")return L"Cortana.UI.Views.TaskbarSearchPage";
    if(f && f.Name()==L"SearchBoxControl")return L"Cortana.UI.Views.RichSearchBoxControl";return winrt::get_class_name(o);
}
sx::Controls::ControlTemplate OuterTemplate(){return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="ContentControl"><Grid Name="RootGrid" Background="#FF202020" BorderBrush="Gray" BorderThickness="1" CornerRadius="16"><ContentPresenter/><Button Name="SearchGlyphContainer" Width="32" HorizontalAlignment="Left" Content="x"/><Grid Name="UnderlineContainer" BorderThickness="0,0,0,2" BorderBrush="White"/></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>();}
sx::Controls::ControlTemplate EditorTemplate(){return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="TextBox"><Grid Background="{TemplateBinding Background}"><ScrollViewer Name="ContentElement" Padding="{TemplateBinding Padding}" HorizontalScrollMode="Auto" HorizontalScrollBarVisibility="Hidden" VerticalScrollBarVisibility="Disabled"/><TextBlock Name="PlaceholderTextContentPresenter" Padding="{TemplateBinding Padding}" Text="{TemplateBinding PlaceholderText}" IsHitTestVisible="False"/></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>();}
int main(){const char* step="initialize";try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
    auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerSearchInputFixture";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower native search input fixture",WS_POPUP,100,100,1000,600,nullptr,nullptr,wc.hInstance,nullptr);
    sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,1000,600,SWP_SHOWWINDOW);
    step="page";sx::Controls::UserControl page;page.Name(L"Page");sx::Controls::Grid pane;pane.Width(832);pane.HorizontalAlignment(sx::HorizontalAlignment::Left);page.Content(pane);
    step="outer template";sx::Controls::ContentControl outer;outer.Name(L"SearchBoxControl");outer.Margin({40,16,40,16});outer.Height(32);outer.VerticalAlignment(sx::VerticalAlignment::Top);outer.Template(OuterTemplate());pane.Children().Append(outer);
    step="editor template";sx::Controls::TextBox text;text.Template(EditorTemplate());text.Name(L"SearchTextBox");text.PlaceholderText(L"Search for apps, settings, and documents");text.Padding({44,0,16,0});text.Text(L"notepad");text.Select(2,3);outer.Content(text);step="attach";host.Content(page);ShowWindow(window,SW_SHOWNOACTIVATE);Layout(pane);
    step="native template snapshot";
    const auto templateBefore=text.Template();const auto outerBefore=outer.Template();auto font=text.FontFamily();auto size=text.FontSize();auto weight=text.FontWeight();auto root=FindStartElement(outer,L"RootGrid").as<sx::Controls::Grid>();auto fill=root.Background();
    auto placeholder=FindStartElement(text,L"PlaceholderTextContentPresenter").as<sx::Controls::TextBlock>();auto nativePlaceholder=placeholder.Text();
    sx::FrameworkElement scroll{nullptr};std::vector<sx::DependencyObject> nodes{text};for(size_t i=0;i<nodes.size() && i<128;++i){if(auto c=nodes[i].try_as<sx::Controls::ScrollContentPresenter>()){scroll=c;break;}for(int j=0;j<sx::Media::VisualTreeHelper::GetChildrenCount(nodes[i]);++j)nodes.push_back(sx::Media::VisualTreeHelper::GetChild(nodes[i],j));}
    Check(bool(scroll),"native editable ScrollContentPresenter discovered");auto nativeMargin=scroll.Margin();
    Check(!IsSearchInput(outer) && IsSearchInput(outer,Supported),"only recognized SearchHost control classes match");
    unsigned changed=0;auto token=text.TextChanged([&](auto&&,auto&&){++changed;});
    {
        SearchInput style(outer,Supported);
        step="apply style";
        for(bool light:{false,true}){
            sx::Media::SolidColorBrush ink(light?winrt::Windows::UI::Color{255,0,0,0}:winrt::Windows::UI::Color{255,255,255,255});text.Foreground(ink);
            style.Refresh(true,light,FixtureGreeting());Layout(pane);style.Refresh(true,light,FixtureGreeting());Layout(pane);Check(style.Applied(),"native Search input is styled");
            Check(text.Template()==templateBefore && outer.Template()==outerBefore && text.Text()==L"notepad" && text.SelectionStart()==2 && text.SelectionLength()==3 && !changed,"applying style preserves native editor text, selection, templates and handlers");
            Check(text.FontFamily()==font && text.FontSize()==size && text.FontWeight().Weight==weight.Weight,"native typography is untouched");
            Check(root.Background().as<sx::Media::SolidColorBrush>().Color().A==0 && root.BorderThickness().Left==0 && placeholder.Text()==FixtureGreeting(),"native pill fill/outline and placeholder styled");
            auto layer=FindStartElement(outer,L"JellyflowerResultsSearchIcon").as<sx::Controls::Canvas>();auto icon=layer.Children().GetAt(0).as<sx::Controls::Canvas>();
            Check(!layer.IsHitTestVisible() && icon.Width()==20 && icon.Height()==20,"noninteractive exact 20-DIP shared magnifier");
            Check(icon.Children().GetAt(0).as<sx::Shapes::Path>().Stroke()==ink,"native primary editor brush colors search artwork");
            for(double width:{832.0,624.0,953.666667}){
                pane.Width(width);Layout(pane);auto p=outer.TransformToVisual(pane).TransformPoint({0,0});auto ep=scroll.TransformToVisual(pane).TransformPoint({0,0});
                Check(std::abs(p.X-32)<1 && std::abs(p.X+outer.ActualWidth()-(width-32))<1,"search fills resized viewport with symmetric header inset");
                Check(std::abs(icon.TransformToVisual(pane).TransformPoint({0,0}).X-32)<1 && std::abs(ep.X-60)<1,"icon and native editor layout align across viewport widths");
                auto divider=FindStartElement(pane,L"JellyflowerResultsSearchDivider").as<sx::Controls::Canvas>().Children().GetAt(0).as<sx::Controls::Border>();
                Check(divider.Width()==pane.ActualWidth() && divider.Height()==1,"thin divider spans the header viewport");
            }
            style.Refresh(true,!light,FixtureGreeting());Check(text.PlaceholderText()==FixtureGreeting() && text.Text()==L"notepad","theme change retains the greeting and query");style.Refresh(true,light,L"Good afternoon, Jelly 🧸");Check(text.PlaceholderText()==L"Good afternoon, Jelly 🧸" && text.Text()==L"notepad" && text.SelectionStart()==2 && text.SelectionLength()==3 && !changed,"controller-supplied greeting never changes query or selection");for(unsigned period=0;period<3;++period){const auto greeting=SearchGreeting(L"Jelly",period);style.Refresh(true,light,greeting);Check(text.PlaceholderText()==greeting && text.Text()==L"notepad" && text.SelectionStart()==2 && text.SelectionLength()==3 && !changed,"clock-driven greetings never change query/selection/events");}style.Refresh(true,light,FixtureGreeting());style.Restore();Layout(pane);Check(outer.Margin().Left==40 && outer.Margin().Right==40,"original outer margins restore");Check(root.Background()==fill,"original Search brush restores");Check(placeholder.Text()==nativePlaceholder,"original Search placeholder restores");Check(scroll.Margin().Left==nativeMargin.Left,"native editor template padding restores");
        }
        style.Refresh(true,false,FixtureGreeting());Layout(pane);text.Text(L"a long native editable query that scrolls horizontally without changing the text model");text.Select(50,4);Layout(pane);Check(changed==1 && text.SelectionStart()==50 && text.SelectionLength()==4,"native text events and selection still work after styling");
        outer.Template(OuterTemplate());Layout(pane);style.Refresh(true,false,FixtureGreeting());Layout(pane);Check(style.Applied(),"replacement Search template is rediscovered");
        style.Refresh(false,false,FixtureGreeting());Check(!style.Applied() && !FindStartElement(pane,L"JellyflowerResultsSearchDivider"),"policy disable removes owned visuals");
        style.Refresh(true,false,FixtureGreeting());Layout(pane);
    }
    Check(!FindStartElement(pane,L"JellyflowerResultsSearchDivider") && !FindStartElement(outer,L"JellyflowerResultsSearchIcon"),"destruction restores Search and removes overlays");
    // Build 26200 puts the input and a separate pill inside a padded page;
    // the actual content viewport is an inset LayerBorder, not the outer page.
    pane.Padding({12,12,12,0});pane.Width(858);outer.Margin({33,17,33,10});
    sx::Controls::Border viewport;viewport.Name(L"LayerBorder");viewport.Margin({1,1,1,1});pane.Children().InsertAt(0,viewport);
    sx::Controls::Border pill;pill.Name(L"TaskbarSearchBackground");pill.Background(fill);pill.Margin(outer.Margin());pill.Height(32);pill.VerticalAlignment(sx::VerticalAlignment::Top);pane.Children().InsertAt(1,pill);Layout(pane);
    sx::Controls::Border frame;frame.Name(L"AppBorder");frame.CornerRadius({8,8,8,8});pane.Children().InsertAt(0,frame);
    viewport.CornerRadius({7,7,7,7});
    sx::Controls::Grid results;results.Name(L"QueryFormulationRoot");results.Margin({3,0,4,5});results.Height(100);results.VerticalAlignment(sx::VerticalAlignment::Top);pane.Children().Append(results);
    {SearchInput style(outer,Supported);style.Refresh(true,false,FixtureGreeting());Layout(pane);
        Check(frame.CornerRadius().TopLeft==20 && viewport.CornerRadius().BottomRight==19,"Search outer and inset background layers share the main Start corner recipe");
        Check(results.Margin().Top==12 && results.Margin().Left==3 && results.Margin().Bottom==5,"results tabs receive their own spacing below the header divider");
        results.Margin({6,2,7,8});Check(results.Margin().Top==12 && results.Margin().Left==6,"native result margin updates preserve the tab gap");
        const auto vp=viewport.TransformToVisual(pane).TransformPoint({0,0});
        auto layer=FindStartElement(outer,L"JellyflowerResultsSearchIcon").as<sx::Controls::Canvas>();auto icon=layer.Children().GetAt(0).as<sx::Controls::Canvas>();
        Check(std::abs(icon.TransformToVisual(pane).TransformPoint({0,0}).X-(vp.X+32))<1,"real padded page aligns artwork to the inset content viewport");
        Check(std::abs(scroll.TransformToVisual(pane).TransformPoint({0,0}).X-(vp.X+60))<1,"real padded page keeps native text/caret at the same inset");
        Check(pill.Background().as<sx::Media::SolidColorBrush>().Color().A==0,"separate native pill background becomes clear");
        auto line=FindStartElement(pane,L"JellyflowerResultsSearchDivider").as<sx::Controls::Canvas>().Children().GetAt(0).as<sx::Controls::Border>();
        const auto lp=line.TransformToVisual(pane).TransformPoint({0,0}),cp=outer.TransformToVisual(pane).TransformPoint({0,0});
        Check(std::abs(lp.X-vp.X)<1 && line.Width()==viewport.ActualWidth(),"padded page divider uses its own layer origin and spans the actual viewport");
        Check(std::abs(lp.Y-(cp.Y+outer.ActualHeight()+outer.Margin().Bottom-1))<1,"padded page divider sits directly below the native input header");
        style.Restore();Layout(pane);
        Check(frame.CornerRadius().TopLeft==8 && viewport.CornerRadius().BottomRight==7 && results.Margin().Top==2 && results.Margin().Left==6,"Search frame and latest native results margins restore on exit");
        Check(pill.Background()==fill && outer.Margin().Left==33 && outer.Margin().Bottom==10,"separate native pill and native page margins restore");
    }
    text.TextChanged(token);host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();std::puts("PASS: native editable Search template, text/selection/events, unchanged typography, borderless header, shared artwork/primary ink, responsive margins/divider, retemplating and restoration.");return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL %s HRESULT %08lx %s\n",step,static_cast<unsigned long>(e.code()),winrt::to_string(e.message()).c_str());}catch(const std::exception& e){std::printf("FAIL %s: %s\n",step,e.what());}return 1;}

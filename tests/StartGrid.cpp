// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_grid.hpp"
#include "../src/start_pinned.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Layout(const sx::Controls::Grid& root){for(unsigned i=0;i<16;++i){root.UpdateLayout();Pump();Sleep(8);}}
winrt::hstring Supported(const sx::DependencyObject& o){auto f=o.try_as<sx::FrameworkElement>();if(f && f.Name()==L"MainContent")return L"StartMenu.StartHome";if(f && f.Name()==L"Category")return L"StartMenu.CategoryControl";if(f && f.Name()==L"PinnedApp")return L"StartMenu.PinnedListTile";if(f && f.Name()==L"PinnedFolder")return L"StartMenu.PinnedListCollapsedFolder";return winrt::get_class_name(o);}
double X(const sx::FrameworkElement& item,const sx::UIElement& root){return item.TransformToVisual(root).TransformPoint({0,0}).X;}
sx::Controls::ControlTemplate Plain(){return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="ContentControl"><ContentPresenter HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"/></ControlTemplate>)").as<sx::Controls::ControlTemplate>();}
struct Item {
    sx::Controls::GridViewItem cell;
    sx::Controls::Grid root;
    sx::Controls::Control tile{nullptr};
    sx::Controls::Grid logo{nullptr};
    sx::Controls::TextBlock label{nullptr};
    sx::Controls::Border square{nullptr};
    sx::Controls::ItemsWrapGrid mini{nullptr};
};
Item Make(unsigned kind){
    Item i;i.root.Name(L"RootGrid");i.cell.MinWidth(0);i.cell.MaxWidth(std::numeric_limits<double>::infinity());i.cell.MinHeight(0);i.cell.Padding({});i.cell.Margin({});
    if(kind==0){
        i.root.Margin({0,9,27,3});
        i.tile=sx::Controls::ItemsControl();i.tile.Name(L"Category");i.tile.Padding({10,10,10,10});
        auto category=i.tile.as<sx::Controls::ItemsControl>();
        category.ItemsPanel(sx::Markup::XamlReader::Load(LR"(<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><ItemsWrapGrid Orientation="Horizontal" MaximumRowsOrColumns="2"/></ItemsPanelTemplate>)").as<sx::Controls::ItemsPanelTemplate>());
        category.Template(sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="ItemsControl"><Grid Name="RootGrid"><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions><Border Width="156" Height="156"/><ScrollViewer Name="ScrollViewer" HorizontalAlignment="Left" Margin="10"><ItemsPresenter/></ScrollViewer><Button Name="SeeAllButton" Grid.Row="1" Width="156" Height="32" Margin="0,4,0,0" Content="category"/></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>());
        for(unsigned n=0;n<4;++n){sx::Controls::Button button;button.Name(n==3?L"FolderPlate":L"LogoContainer");button.Width(68);button.Height(68);category.Items().Append(button);}
        i.root.Children().Append(i.tile);
    }else if(kind==1 || kind==2){
        i.root.Width(96);i.root.Height(84);
        i.tile=sx::Controls::ContentControl();i.tile.Name(kind==1?L"PinnedApp":L"PinnedFolder");i.tile.Template(Plain());
        auto body=sx::Markup::XamlReader::Load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Name="Root" Padding="0,12,0,0"><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions><Grid Name="LogoContainer" Width="32" Height="32" HorizontalAlignment="Center"/><Grid Name="DisplayNameAndDownloadIconContainer" Grid.Row="1"><TextBlock Name="DisplayName" Text="native accessible app name" Margin="2,4,2,0"/></Grid></Grid>)").as<sx::Controls::Grid>();
        i.logo=FindStartElement(body,L"LogoContainer").as<sx::Controls::Grid>();i.label=FindStartElement(body,L"DisplayName").as<sx::Controls::TextBlock>();i.tile.as<sx::Controls::ContentControl>().Content(body);i.root.Children().Append(i.tile);
        if(kind==2){i.logo.Name(L"LogosContainer");i.logo.Width(40);i.logo.Height(40);body.Padding({0,8,0,0});}
    }else {
        const bool rows=kind>=4;
        i.root.Width(rows?728:160);i.root.Margin({0,0,rows?0.0:20.0,0});
        sx::Controls::ColumnDefinition icon,text;icon.Width({44,sx::GridUnitType::Pixel});i.root.ColumnDefinitions().Append(icon);i.root.ColumnDefinitions().Append(text);
        i.logo=sx::Controls::Grid();i.logo.Width(32);i.logo.Height(32);i.logo.Margin({12,0,0,0});i.logo.HorizontalAlignment(sx::HorizontalAlignment::Left);i.root.Children().Append(i.logo);
        i.label=sx::Controls::TextBlock();i.label.Text(L"native app name");sx::Controls::Grid::SetColumn(i.label,1);i.root.Children().Append(i.label);i.cell.Height(rows?40:84);
    }
    if(kind==7){
        // Current React Recommended containers arrive with these constraints,
        // unlike the unconstrained app-grid wrappers used above.
        i.root.Width(std::numeric_limits<double>::quiet_NaN());i.root.MinWidth(212);i.root.MaxWidth(212);
        i.cell.MinWidth(236);i.cell.MaxWidth(236);i.cell.Padding({12,0,12,0});i.cell.Margin({0,0,20,0});
    }
    i.cell.Content(i.root);return i;
}
int main(){try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerGridFixture";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower grid fixture",WS_POPUP,100,100,1200,1200,nullptr,nullptr,wc.hInstance,nullptr);
    sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,1200,1200,SWP_SHOWWINDOW);
    sx::Controls::Grid pane;pane.Name(L"MainContent");pane.Width(832);host.Content(pane);
    {
        DeferredStartGrids discovery;unsigned adopted=0;
        auto adopt=[&](auto,const auto&){++adopted;};
        sx::Controls::GridView first;first.Name(L"RecommendedList");
        discovery.Add(1,first);discovery.Discover(adopt,Supported);Check(adopted==0,"a list published before its ancestors waits for a recognized Start context");
        pane.Children().Append(first);Layout(pane);discovery.Discover(adopt,Supported);discovery.Discover(adopt,Supported);Check(adopted==1,"late-connected Recommended list is adopted exactly once");
        pane.Children().Clear();sx::Controls::GridView replacement;replacement.Name(L"RecommendedList");
        discovery.Add(2,replacement);pane.Children().Append(replacement);Layout(pane);discovery.Discover(adopt,Supported);Check(adopted==2,"a replacement React list is adopted independently");
        sx::Controls::GridView removed;removed.Name(L"RecommendedList");discovery.Add(3,removed);discovery.Remove(3);pane.Children().Append(removed);discovery.Discover(adopt,Supported);Check(adopted==2,"removed pending lists cannot be adopted later");
        pane.Children().Clear();
    }
    struct Variation{const wchar_t* name;unsigned kind;int maximum;double parentInset;double pager;};
    unsigned cases=0;
    for(const auto v:{Variation{L"AllAppsGrid",0,4,0,0},Variation{L"PinnedList",1,-1,0,24},Variation{L"PinnedList",2,-1,0,0},
        Variation{L"AllAppsGrid",3,4,0,0},Variation{L"AllAppsGrid",4,1,0,0},Variation{L"RecommendedList",5,3,12,0},Variation{L"RecommendedList",6,1,52,0},Variation{L"RecommendedList",7,-1,20,0}}){
        sx::Controls::Grid parent;parent.Margin({v.parentInset,0,v.parentInset,0});
        if(v.pager){sx::Controls::ColumnDefinition main,indicator;indicator.Width({v.pager,sx::GridUnitType::Pixel});parent.ColumnDefinitions().Append(main);parent.ColumnDefinitions().Append(indicator);}
        pane.Children().Append(parent);
        auto view=sx::Markup::XamlReader::Load(LR"(<GridView xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" VerticalAlignment="Top" Padding="0"><GridView.ItemsPanel><ItemsPanelTemplate><ItemsWrapGrid Orientation="Horizontal" Margin="64,0,0,14"/></ItemsPanelTemplate></GridView.ItemsPanel></GridView>)").as<sx::Controls::GridView>();view.Name(v.name);
        if(v.kind==5)view.Width(808);if(v.kind==6)view.Width(728);
        std::vector<Item> items;for(unsigned j=0;j<20;++j){items.push_back(Make(v.kind));view.Items().Append(items.back().cell);}
        parent.Children().Append(view);Layout(pane);auto panel=view.ItemsPanelRoot().as<sx::Controls::ItemsWrapGrid>();panel.MaximumRowsOrColumns(v.maximum);
        if(v.kind==5)panel.ItemWidth(236);
        if(v.kind==0){auto body=FindStartElement(items[0].tile,L"RootGrid").as<sx::Controls::Grid>();items[0].square=body.Children().GetAt(0).as<sx::Controls::Border>();}
        const double originalNative=items[0].cell.ActualWidth();const auto originalPanel=panel.Margin();const auto originalWidth=view.Width();
        if(v.kind!=7)Check(std::isnan(items[0].cell.Width()) && items[0].cell.MinWidth()==0 && !std::isfinite(items[0].cell.MaxWidth()),"real-shell wrapper has no fixed width constraints");
        std::vector<std::unique_ptr<StartPinned>> pins;
        {
            StartGrid style(view,Supported);style.Refresh(true);style.Restore();style.Refresh(true);
            if(v.kind==1 || v.kind==2)for(auto& item:items){pins.push_back(std::make_unique<StartPinned>(item.tile));pins.back()->Refresh(true);}
            for(double width:{832.0,751.333333,624.0,953.666667}) {
                pane.Width(width);Layout(pane);++cases;
                const bool rows=v.kind>=4;const double edge=rows?StartMetrics::RowInset:StartMetrics::SectionInset;
                const auto first=items[0].cell;unsigned n=1;for(;n<items.size();++n)if(std::abs(items[n].cell.TransformToVisual(pane).TransformPoint({0,0}).Y-first.TransformToVisual(pane).TransformPoint({0,0}).Y)>2)break;
                const auto last=items[n-1].cell;
                const double left=X(first,pane),right=pane.ActualWidth()-X(last,pane)-last.ActualWidth();
                std::printf("view=%ls kind=%u width=%.3f columns=%u gaps=%.3f/%.3f cell=%.3f\n",v.name,v.kind,width,n,left,right,panel.ItemWidth());
                Check(std::abs(left-edge)<1.5 && std::abs(right-edge)<1.5,"actual realized cells fill the viewport with the same side inset");
                Check(std::abs(items[0].root.ActualWidth()+first.Padding().Left+first.Padding().Right-first.ActualWidth())<1.5,"inner data-template root fills the resized item's content area");
                for(unsigned j=1;j<n;++j)Check(std::abs((X(items[j].cell,pane)-X(items[j-1].cell,pane))-panel.ItemWidth())<1.5,"equal horizontal pitch across every complete row");
                if(v.kind==0){
                    Check(std::abs(items[0].square.ActualWidth()-first.ActualWidth())<1.5 && std::abs(items[0].square.ActualHeight()-first.ActualWidth())<1.5,"category card remains square and fills its responsive cell");
                    auto button=FindStartElement(items[0].tile,L"SeeAllButton");Check(std::abs(button.ActualWidth()-first.ActualWidth())<1.5,"category caption/hover target fills card width");
                    auto scroll=FindStartElement(items[0].tile,L"ScrollViewer");Check(std::abs(scroll.ActualWidth()+20-first.ActualWidth())<1.5 && scroll.Margin().Left==10 && scroll.Margin().Right==10,"category inner grid keeps symmetric native padding while resizing");
                }
                if(v.kind==1 || v.kind==2){
                    auto logo=items[0].logo;const auto p=logo.TransformToVisual(first).TransformPoint({0,0});
                    Check(items[0].label.Visibility()==sx::Visibility::Collapsed,"pinned app/folder caption hidden");
                    Check(std::abs(p.X+(logo.ActualWidth()-first.ActualWidth())/2)<1.5 && std::abs(p.Y+(logo.ActualHeight()-first.ActualHeight())/2)<1.5,"pinned app/folder icon centered in its actual target");
                    Check(sx::Automation::AutomationProperties::GetName(first)==L"native accessible app name" && sx::Controls::ToolTipService::GetToolTip(items[0].tile),"pinned identity retained through native automation/tooltip");
                }
                if(v.kind==4 || v.kind==6)Check(n==1,"both list and expanded Show all stay single-column and full width");
            }
            const unsigned before=style.LayoutPasses();for(unsigned j=0;j<40;++j){Pump();Sleep(3);}Check(style.LayoutPasses()==before,"stable layout does not continually schedule adapter work");
            if(v.kind==1 || v.kind==2){items[1].label.Text(L"renamed native caption");Check(sx::Automation::AutomationProperties::GetName(items[1].cell)==L"renamed native caption" && winrt::unbox_value<winrt::hstring>(sx::Controls::ToolTipService::GetToolTip(items[1].tile))==L"renamed native caption","renaming retains current tooltip and accessible identity without showing a caption");}
            // A reused container receives a new native content template.
            auto replacement=Make(v.kind);replacement.cell.Content(nullptr);items[0].cell.Content(replacement.root);style.Invalidate();Layout(pane);Check(replacement.root.ActualWidth()>0,"recycled template is rediscovered");
            style.Restore();for(auto& pin:pins)pin->Restore();
        }
        Check((std::isnan(originalWidth) && std::isnan(view.Width())) || view.Width()==originalWidth,"stock view width restored");
        Check(panel.Margin().Left==originalPanel.Left && panel.Margin().Right==originalPanel.Right,"stock panel margins restored");
        if(v.kind==1 || v.kind==2)Check(items[1].label.Visibility()==sx::Visibility::Visible && items[1].root.Width()==96 && items[1].root.Height()==84,"pinned captions and original template dimensions restore");
        if(v.kind==4 || v.kind==6)Check(items[1].root.Width()==728,"stock list's nested fixed width restored");
        if(v.kind==7)Check(items[1].cell.MinWidth()==236 && items[1].cell.MaxWidth()==236 && items[1].root.MaxWidth()==212 && items[1].cell.Margin().Right==20,"React Recommended native wrapper/content constraints restore");
        Check(originalNative>0,"native template provided a real starting width");pane.Children().Clear();pane.Width(832);Layout(pane);
    }
    host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();
    std::printf("PASS: %u live XAML layout cases with unconstrained native wrappers, nested fixed templates, category cards, pinned apps/folders, list/grid and compact/expanded Recommended; centered icons, identity, resize/recycling/idle/restoration.\n",cases);return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx %s\n",static_cast<unsigned long>(e.code()),winrt::to_string(e.message()).c_str());}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;}

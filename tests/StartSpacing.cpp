// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_spacing.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
#include <memory>
using namespace acrylic;
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void Layout(const sx::Controls::Grid& root){for(unsigned i=0;i<8;++i){root.UpdateLayout();Pump();Sleep(10);}}
winrt::hstring Supported(const sx::DependencyObject& element){auto f=element.try_as<sx::FrameworkElement>();return f && f.Name()==L"MainContent" ? L"StartMenu.StartHome" : winrt::get_class_name(element);}
int main(){try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerSpacingFixture";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    const auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower spacing fixture",WS_POPUP,100,100,1000,600,nullptr,nullptr,wc.hInstance,nullptr);
    sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,1000,600,SWP_SHOWWINDOW);
    auto root=sx::Markup::XamlReader::Load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Name="MainContent" Width="640">
      <TextBlock Name="PinnedListHeaderText" Text="Pinned" Margin="63,6,12,7" VerticalAlignment="Top"/>
      <TextBlock Name="AllListHeadingText" Text="All" Margin="63,6,12,7" VerticalAlignment="Top"/>
      <Grid Name="ShowMorePinnedGrid"><Button Name="ShowPinned" Margin="0,0,52,0"/></Grid>
      <Grid Name="NavPanePlaceholder" Margin="52,0,52,0" VerticalAlignment="Bottom" Height="40"/>
      <Grid Name="TopLevelSuggestionsContainer" Margin="52,0,0,0"/>
      <Button Name="ViewSelectionButton" Margin="0,0,52,0"/>
      <GridView Name="AllAppsGrid" VerticalAlignment="Top" Height="160"><GridView.ItemsPanel><ItemsPanelTemplate><ItemsWrapGrid Name="AppCells" Margin="52,0,52,14"/></ItemsPanelTemplate></GridView.ItemsPanel><TextBlock Text="fixture"/></GridView>
      <GridView Name="OtherList" VerticalAlignment="Bottom" Height="160"><GridView.ItemsPanel><ItemsPanelTemplate><ItemsWrapGrid Name="OtherCells" Margin="52,0,52,14"/></ItemsPanelTemplate></GridView.ItemsPanel><TextBlock Text="fixture"/></GridView>
    </Grid>)").as<sx::Controls::Grid>();
    host.Content(root);Layout(root);
    auto heading=FindStartElement(root,L"PinnedListHeaderText");auto footer=FindStartElement(root,L"NavPanePlaceholder");
    auto cells=FindStartElement(root,L"AppCells");auto other=FindStartElement(root,L"OtherCells");
    Check(cells && other,"native list templates realized");
    Check(StartSpacingFor(heading)==StartSpacingRule::None,"unrelated XAML tree rejected");
    Check(StartSpacingFor(cells,Supported)==StartSpacingRule::None && StartSpacingFor(other,Supported)==StartSpacingRule::None,"grid panels are owned only by the responsive grid adapter");
    std::vector<std::unique_ptr<StartSpacing>> adapters;
    for(const auto name:{L"PinnedListHeaderText",L"AllListHeadingText",L"ShowPinned",L"NavPanePlaceholder",L"TopLevelSuggestionsContainer",L"ViewSelectionButton"}) {
        const auto element=FindStartElement(root,name);const auto rule=StartSpacingFor(element,Supported);
        Check(rule!=StartSpacingRule::None,"known native spacing anchor recognized");
        adapters.push_back(std::make_unique<StartSpacing>(element,rule));adapters.back()->Refresh(true);
    }
    Layout(root);
    Check(heading.Margin().Left==32 && heading.Margin().Top==6 && heading.Margin().Bottom==7,"headings compact without changing vertical spacing");
    Check(cells.Margin().Left==52 && cells.Margin().Right==52,"spacing adapter leaves grid measurement to StartGrid");
    Check(footer.Margin().Left==20 && footer.Margin().Right==20 && footer.ActualWidth()==600,"footer expands through native layout rather than a translated overlay");
    Check(FindStartElement(root,L"ShowPinned").Margin().Right==20 && FindStartElement(root,L"ViewSelectionButton").Margin().Right==20,"right-side controls follow the same compact inset");
    Check(other.Margin().Left==52,"unknown list remains stock");
    footer.Margin({60,4,60,18});Check(footer.Margin().Left==20 && footer.Margin().Right==20 && footer.Margin().Top==4 && footer.Margin().Bottom==18,"later shell margin changes retain vertical updates and symmetric insets");
    for(auto& adapter:adapters)adapter->Refresh(false);
    Check(heading.Margin().Left==63 && footer.Margin().Left==60 && footer.Margin().Top==4,"disable restores latest stock margins");
    Check(CompactStartMargin({4,0,6,0},StartSpacingRule::Left).Left==4,"already-small insets are never enlarged");
    sx::Data::Binding binding;binding.Source(winrt::box_value(sx::Thickness{52,3,52,9}));footer.SetBinding(sx::FrameworkElement::MarginProperty(),binding);
    {StartSpacing spacing(footer,StartSpacingRule::Both);spacing.Refresh(true);Check(footer.Margin().Left==20,"bound margin is compacted");}
    Check(footer.GetBindingExpression(sx::FrameworkElement::MarginProperty()).ParentBinding()==binding && footer.Margin().Left==52,"destructor restores original binding");
    heading.ClearValue(sx::FrameworkElement::MarginProperty());
    {StartSpacing spacing(heading,StartSpacingRule::Heading);spacing.Refresh(true);Check(heading.Margin().Left==32,"unset margin gets the compact heading inset");}
    Check(heading.ReadLocalValue(sx::FrameworkElement::MarginProperty())==sx::DependencyProperty::UnsetValue(),"unset value restored without adding a local style");
    host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();
    std::puts("PASS: recognized native Start headings/apps/footer/right controls, symmetrical compact layout, unknown-list guard, vertical updates and latest/local/unset/binding/destructor restoration.");return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx\n",static_cast<unsigned long>(e.code()));}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;}

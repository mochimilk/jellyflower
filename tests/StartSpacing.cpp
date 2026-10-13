// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_spacing.hpp"
#include "../src/start_user.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <cmath>
using namespace acrylic;
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void Layout(const sx::Controls::Grid& root){for(unsigned i=0;i<8;++i){root.UpdateLayout();Pump();Sleep(10);}}
winrt::hstring Supported(const sx::DependencyObject& element){auto f=element.try_as<sx::FrameworkElement>();return f && f.Name()==L"MainContent" ? L"StartMenu.StartHome" : winrt::get_class_name(element);}
winrt::hstring UserType(const sx::DependencyObject& element){auto f=element.try_as<sx::FrameworkElement>();if(f && f.Name()==L"UserTileButton")return L"StartDocked.NavigationPaneButton";if(f && f.Name()==L"UserTile")return L"StartDocked.UserTileView";return winrt::get_class_name(element);}
sx::Controls::ControlTemplate UserTemplate(){return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="Button"><Grid><Border Name="BackgroundBorder" CornerRadius="4" Background="#20404040"/><ContentPresenter Name="ContentPresenter" Padding="{TemplateBinding Padding}" BorderThickness="{TemplateBinding BorderThickness}" HorizontalAlignment="Stretch" VerticalAlignment="Center"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><Grid Name="UserTileIcon" Width="32" Height="32" VerticalAlignment="Center"><Ellipse Fill="Gray"/></Grid><TextBlock Name="UserTileNameText" Grid.Column="1" Margin="12,0,0,0" VerticalAlignment="Center" Text="Fixture account"/></Grid></ContentPresenter></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>();}
void UserChecks(const sx::Hosting::DesktopWindowXamlSource& host){
    sx::Controls::Grid root;root.Width(640);root.Name(L"UserTile");root.Height(40);root.HorizontalAlignment(sx::HorizontalAlignment::Left);
    sx::Controls::Button button;button.Name(L"UserTileButton");button.HorizontalAlignment(sx::HorizontalAlignment::Left);button.Template(UserTemplate());button.Padding({11,0,11,0});button.BorderThickness({1,1,1,1});root.Children().Append(button);
    sx::Controls::ToolTipService::SetToolTip(button,winrt::box_value(L"Fixture account"));bool clicked=false;const auto token=button.Click([&](auto&&,auto&&){clicked=true;});
    host.Content(root);Layout(root);const auto nativeTemplate=button.Template();
    Check(!IsStartUserButton(button) && IsStartUserButton(button,UserType),"profile styles reject unrelated same-named controls");
    auto icon=FindStartElement(button,L"UserTileIcon");auto background=FindStartElement(button,L"BackgroundBorder").as<sx::Controls::Border>();auto label=FindStartElement(button,L"UserTileNameText").as<sx::Controls::TextBlock>();
    const auto originalFont=label.FontFamily().Source();const auto originalSize=label.FontSize();
    StartUser user(button);user.Refresh(true);Layout(root);
    Check(user.Applied() && icon.ActualWidth()==28 && icon.ActualHeight()==28 && button.ActualHeight()==40,"one-step-smaller avatar retains the full native click target");
    const auto position=icon.TransformToVisual(button).TransformPoint({0,0});const auto text=label.TransformToVisual(button).TransformPoint({0,0});
    std::printf("Badge avatar=%g,%g target=%g,%g edge=%g,%g,%g\n",icon.ActualWidth(),icon.ActualHeight(),button.ActualWidth(),button.ActualHeight(),position.X,position.Y,button.ActualWidth()-(text.X+label.ActualWidth()));
    Check(std::abs(position.X-6)<1 && std::abs(position.Y-6)<1 && std::abs(button.ActualWidth()-(text.X+label.ActualWidth())-6)<1,"hover container has equal six-DIP padding within native fractional-DPI rounding");
    Check(background.CornerRadius().TopLeft==10 && button.Template()==nativeTemplate && button.IsTabStop(),"larger radius retains native template and keyboard target");
    Check(label.FontFamily().Source()==originalFont && label.FontSize()==originalSize && winrt::unbox_value<winrt::hstring>(sx::Controls::ToolTipService::GetToolTip(button))==L"Fixture account","account typography and tooltip retained");
    auto peer=sx::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);peer.GetPattern(sx::Automation::Peers::PatternInterface::Invoke).as<sx::Automation::Provider::IInvokeProvider>().Invoke();Pump();Check(clicked,"native account invocation still reaches its handler");
    button.Padding({16,2,16,2});Check(button.Padding().Left==5,"later native hover/layout padding cannot undo the compact badge");user.Restore();Layout(root);
    Check(icon.Width()==32 && icon.Height()==32 && background.CornerRadius().TopLeft==4 && button.Padding().Left==16 && button.Padding().Top==2,"latest native sizing and visual-state values restore");
    sx::Data::Binding binding;binding.Source(winrt::box_value(32.0));icon.SetBinding(sx::FrameworkElement::WidthProperty(),binding);user.Refresh(true);user.Restore();Check(icon.GetBindingExpression(sx::FrameworkElement::WidthProperty()).ParentBinding()==binding,"avatar width binding restores");
    user.Refresh(true);button.Template(UserTemplate());Layout(root);user.Refresh(true);Layout(root);Check(FindStartElement(button,L"UserTileIcon").Width()==28,"replacement profile template is restyled");
    user.Refresh(false);Check(FindStartElement(button,L"UserTileIcon").Width()==32,"disabled adapter restores replacement template");button.Click(token);
}
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
    UserChecks(host);
    host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();
    std::puts("PASS: recognized native Start headings/apps/footer/right controls, symmetrical compact layout, unknown-list guard, vertical updates and latest/local/unset/binding/destructor restoration.");return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx\n",static_cast<unsigned long>(e.code()));}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;}

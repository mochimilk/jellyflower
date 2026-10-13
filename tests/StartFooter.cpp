// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_footer.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <memory>
using namespace acrylic;
void Check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void Layout(const sx::Controls::Grid& root){for(unsigned n=0;n<8;++n){root.UpdateLayout();MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(8);}}
winrt::hstring Supported(const sx::DependencyObject& object){
    auto f=object.try_as<sx::FrameworkElement>();if(!f)return winrt::get_class_name(object);
    if(f.Name()==L"MainContent")return L"StartMenu.StartHome";
    if(f.Name()==L"Shortcut")return L"StartDocked.AppListViewItem";
    if(f.Name()==L"PowerButton")return L"StartDocked.NavigationPaneButton";
    if(f.Name()==L"PowerHost")return L"StartDocked.PowerOptionsView";
    if(f.Name()==L"NavigationPanePlacesListView")return L"StartDocked.AppListView";
    return winrt::get_class_name(object);
}
sx::Controls::ControlTemplate Template(){return sx::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="Button"><Grid><Border Name="BackgroundBorder" CornerRadius="4" Background="#40404040"/><ContentPresenter Name="ContentPresenter" HorizontalAlignment="Center" VerticalAlignment="Center"/></Grid></ControlTemplate>)").as<sx::Controls::ControlTemplate>();}
int main(){try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerFooterFixture";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"footer fixture",WS_POPUP,100,100,800,700,nullptr,nullptr,wc.hInstance,nullptr);
    sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,800,700,SWP_SHOWWINDOW);
    sx::Controls::Grid root;root.Name(L"MainContent");root.Width(640);root.Height(600);
    sx::Controls::RowDefinition content,footerRow;footerRow.Height({64,sx::GridUnitType::Pixel});root.RowDefinitions().Append(content);root.RowDefinitions().Append(footerRow);
    sx::Controls::Grid footer;footer.Name(L"NavPanePlaceholder");footer.Height(44);footer.VerticalAlignment(sx::VerticalAlignment::Top);sx::Controls::Grid::SetRow(footer,1);root.Children().Append(footer);
    sx::Controls::StackPanel places;places.Name(L"NavigationPanePlacesListView");places.Orientation(sx::Controls::Orientation::Horizontal);places.VerticalAlignment(sx::VerticalAlignment::Top);footer.Children().Append(places);
    std::vector<sx::Controls::Button> buttons;
    sx::Controls::ContentControl powerHost;powerHost.Name(L"PowerHost");powerHost.Height(40);powerHost.VerticalAlignment(sx::VerticalAlignment::Center);powerHost.HorizontalContentAlignment(sx::HorizontalAlignment::Stretch);powerHost.VerticalContentAlignment(sx::VerticalAlignment::Stretch);
    for(const auto name:{L"Shortcut",L"PowerButton"}){sx::Controls::Button button;button.Name(name);button.Width(40);if(std::wstring_view(name)!=L"PowerButton")button.Height(40);button.Template(Template());button.VerticalAlignment(sx::VerticalAlignment::Top);button.Content(winrt::box_value(L"x"));if(button.Name()==L"PowerButton"){powerHost.Content(button);places.Children().Append(powerHost);}else places.Children().Append(button);buttons.push_back(button);}
    host.Content(root);Layout(root);
    Check(IsStartFooter(footer,Supported) && !IsStartFooter(footer),"footer requires native Start context");
    StartFooter frame(footer);StartFooterPart hostStyle(places,StartFooterPartFor(places,Supported));
    Check(std::isnan(buttons[1].Height()),"power fixture reproduces native automatic height rather than assuming a square");
    std::vector<std::unique_ptr<StartFooterPart>> parts;for(const auto& button:buttons){Check(StartFooterPartFor(button,Supported)==StartFooterKind::Action,"shortcut and power recognized only inside footer");parts.push_back(std::make_unique<StartFooterPart>(button,StartFooterKind::Action,Supported));}
    frame.Refresh(true);hostStyle.Refresh(true);for(auto& part:parts)part->Refresh(true);Layout(root);
    Check(std::abs(footerRow.ActualHeight()-80)<.1 && footerRow.Height().Value==64,"native footer row grows by sixteen while its Height stays intact");
    const auto center=root.ActualHeight()-footerRow.ActualHeight()/2;
    for(const auto& button:buttons){const auto y=button.TransformToVisual(root).TransformPoint({0,0}).Y;Check(std::abs(y+button.ActualHeight()/2-center)<1,"footer actions vertically centered in expanded row");Check(FindStartElement(button,L"BackgroundBorder").as<sx::Controls::Border>().CornerRadius().TopLeft==12,"shortcut and power hover corners rounded");}
    auto powerBackground=FindStartElement(buttons[1],L"BackgroundBorder").as<sx::Controls::Border>();Check(std::abs(powerBackground.ActualHeight()-powerBackground.ActualWidth())<.1 && buttons[1].ActualHeight()==40,"native auto-height power hover target becomes square");
    buttons[1].Width(44);Layout(root);Check(buttons[1].ActualHeight()==44 && powerHost.Height()==44 && std::abs(powerBackground.ActualWidth()-powerBackground.ActualHeight())<.1,"power and outer host follow native width changes without clipping");buttons[1].Width(40);Layout(root);
    const auto oldTemplate=buttons[0].Template();buttons[0].Template(Template());Layout(root);parts[0]->Refresh(true);Check(FindStartElement(buttons[0],L"BackgroundBorder").as<sx::Controls::Border>().CornerRadius().TopLeft==12 && buttons[0].Template()!=oldTemplate,"replacement native action template rounded");
    auto background=FindStartElement(buttons[1],L"BackgroundBorder").as<sx::Controls::Border>();background.CornerRadius({6,6,6,6});Check(background.CornerRadius().TopLeft==12,"later native corner updates remain styled");
    sx::Data::Binding heightBinding;heightBinding.Source(winrt::box_value(sx::GridLength{72,sx::GridUnitType::Pixel}));sx::Data::BindingOperations::SetBinding(footerRow,sx::Controls::RowDefinition::HeightProperty(),heightBinding);Layout(root);
    Check(footerRow.Height().Value==72 && footerRow.ActualHeight()==88,"bound native row height updates preserve sixteen extra DIP");
    footerRow.MinHeight(94);Layout(root);Check(footerRow.MinHeight()==94,"explicit native minimum takes precedence");frame.Restore();Check(footerRow.MinHeight()==94,"new native minimum preserved on exit");
    footerRow.ClearValue(sx::Controls::RowDefinition::MinHeightProperty());frame.Refresh(true);Layout(root);Check(footerRow.ActualHeight()==88,"cleared native minimum allows expansion again");
    frame.Restore();hostStyle.Restore();for(auto& part:parts)part->Restore();Layout(root);
    Check(footerRow.ReadLocalValue(sx::Controls::RowDefinition::MinHeightProperty())==sx::DependencyProperty::UnsetValue() && footerRow.Height().Value==72 && footerRow.ActualHeight()==72,"stock row minimum and bound Height restored");
    Check(footer.VerticalAlignment()==sx::VerticalAlignment::Top && places.VerticalAlignment()==sx::VerticalAlignment::Top && background.CornerRadius().TopLeft==6,"native alignments and latest action corner restored");
    Check(std::isnan(buttons[1].Height()) && powerHost.Height()==40,"native automatic power height and outer fixed height restore");
    footerRow.Height({1,sx::GridUnitType::Star});frame.Refresh(true);Check(footerRow.ReadLocalValue(sx::Controls::RowDefinition::MinHeightProperty())==sx::DependencyProperty::UnsetValue(),"unknown flexible footer rows retain native responsive sizing");frame.Restore();
    places.Children().Clear();footer.Children().Clear();root.Children().Append(buttons[0]);Check(StartFooterPartFor(buttons[0],Supported)==StartFooterKind::None,"same-named app row outside footer rejected");
    host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();
    std::puts("PASS: expanded native footer row, vertical centers, shortcut/power corners, retemplate, later native updates, bindings, restore and guards.");return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx %s\n",static_cast<unsigned long>(e.code()),winrt::to_string(e.message()).c_str());}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;}

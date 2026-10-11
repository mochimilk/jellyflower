// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_style.hpp"
#include "../src/start_corners.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <dwmapi.h>
#include <fstream>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void Pump() {MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
void CompositionChecks() {
    WNDCLASSW type{};type.lpszClassName=L"jellyflower.StartMaterialFixture";type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&type);
    const HWND window=CreateWindowExW(WS_EX_TOPMOST,type.lpszClassName,L"jellyflower Start material fixture",WS_POPUP|WS_CLIPCHILDREN,100,100,360,240,nullptr,nullptr,type.hInstance,nullptr);
    Check(window!=nullptr,"fixture window created");
    sx::Hosting::DesktopWindowXamlSource host;
    auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));
    HWND child=nullptr;winrt::check_hresult(native->get_WindowHandle(&child));SetWindowPos(child,nullptr,0,0,360,240,SWP_SHOWWINDOW);
    sx::Controls::Grid grid;
    sx::Media::SolidColorBrush background;grid.Background(background);
    sx::Controls::Border border;border.Name(L"AcrylicBorder");border.Margin({20,20,20,20});grid.Children().Append(border);
    host.Content(grid);ShowWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);Pump();
    {
        StartSurface surface(border);
        for(bool light:{false,true}) {
            surface.Refresh(true,light);Pump();
            auto material=border.Background().as<sx::Media::XamlCompositionBrushBase>();
            for(unsigned i=0;i<25 && !material.as<sm::IXamlCompositionBrushBaseProtected>().CompositionBrush();++i){Pump();Sleep(20);}
            Check(material.as<sm::IXamlCompositionBrushBaseProtected>().CompositionBrush().try_as<sc::CompositionEffectBrush>()!=nullptr,"XAML connects the production blur effect graph");
            // Connection precedes the compositor's first presented frame.
            // Let the new graph become live before changing its backdrop.
            for(unsigned i=0;i<15;++i){Pump();Sleep(20);}DwmFlush();
            COLORREF samples[2]{};
            for(unsigned color=0;color<2;++color) {
                background.Color(color?winrt::Windows::UI::Color{255,30,30,220}:winrt::Windows::UI::Color{255,220,30,30});
                for(unsigned i=0;i<15;++i){Pump();Sleep(20);}DwmFlush();
                HDC screen=GetDC(nullptr);samples[color]=GetPixel(screen,200,170);
                const auto outside=GetPixel(screen,110,110);
                Check(outside==(color?RGB(30,30,220):RGB(220,30,30)),"uncovered fixture background matches the controlled color");
                ReleaseDC(nullptr,screen);
            }
            std::printf("Start compositor %s: red=%06lx blue=%06lx\n",light?"light":"dark",samples[0],samples[1]);
            Check(GetRValue(samples[0])>GetRValue(samples[1])+30 && GetBValue(samples[1])>GetBValue(samples[0])+30,"production Start material visibly responds to pixels underneath in both themes");
            const DWORD tint=MenuTint(light);const double alpha=(tint>>24)/255.0;
            const int expected=static_cast<int>(220*(1-alpha)+(tint&255)*alpha);
            Check(std::abs(static_cast<int>(GetRValue(samples[0]))-expected)<=5,"single tint opacity matches the shared menu recipe rather than stacking two layers");
        }
    }
    // Native shadow/background layers used a smaller radius than the material.
    // Check the actual compositor, not just the CornerRadius property.
    grid.Children().Clear();background.Color({255,220,30,30});
    sx::Controls::Border outline;outline.Margin({20,20,20,20});outline.CornerRadius({20,20,20,20});outline.Background(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,50,50,50}));grid.Children().Append(outline);
    sx::Controls::Grid content;content.Margin({21,21,21,21});
    content.Background(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{255,30,30,220}));grid.Children().Append(content);
    auto settle=[&]{for(unsigned i=0;i<15;++i){grid.UpdateLayout();Pump();Sleep(15);}DwmFlush();};settle();
    const double scale=grid.XamlRoot().RasterizationScale();
    auto corner=[&](bool bottom){
        auto p=content.TransformToVisual(grid).TransformPoint({0,0});
        const double x=bottom?p.X+content.ActualWidth()-3:p.X+3;
        const double y=bottom?p.Y+content.ActualHeight()-3:p.Y+3;
        HDC screen=GetDC(nullptr);auto color=GetPixel(screen,100+static_cast<int>(x*scale),100+static_cast<int>(y*scale));ReleaseDC(nullptr,screen);return color;
    };
    Check(corner(false)==RGB(30,30,220) && corner(true)==RGB(30,30,220),"square background reproduces both corner mismatches");
    {
        StartCorners corners(content,StartCornerRule::Content);corners.Refresh(true);settle();
        Check(corner(false)==RGB(220,30,30) && corner(true)==RGB(220,30,30),"native background radius matches both outer corners");
        SetWindowPos(window,nullptr,0,0,420,280,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);SetWindowPos(child,nullptr,0,0,420,280,SWP_NOZORDER|SWP_NOACTIVATE);settle();
        Check(corner(true)==RGB(220,30,30),"native rounded background tracks the resized bottom corner");
    }
    settle();Check(corner(true)==RGB(30,30,220),"exit restores the original square background");
    host.Content(nullptr);host.Close();DestroyWindow(window);Pump();
}
int main() {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        CompositionChecks();
        {
            sx::Controls::Border border; border.Name(L"AcrylicBorder");
            sx::Media::SolidColorBrush original(winrt::Windows::UI::Color{255,10,20,30});
            border.Background(original);border.CornerRadius({4,4,4,4});
            Check(!IsStartSurface(border),"unrelated named border is rejected");
            {
                StartSurface surface(border);
                for(unsigned cycle=0;cycle<3;++cycle) for(bool light:{false,true}) {
                    surface.Refresh(true,light);Check(surface.Applied(),"applied");
                    auto brush=border.Background().try_as<sx::Media::XamlCompositionBrushBase>();
                    Check(brush!=nullptr,"custom backdrop material");
                    Check(brush.FallbackColor().R==(light?249:32),"shared menu fallback tint");
                    const auto stroke=border.BorderBrush().as<sx::Media::SolidColorBrush>().Color();
                    Check(stroke.R==(light?210:69) && border.BorderThickness().Left==1 && border.CornerRadius().TopLeft==20,"menu stroke and larger radius");
                    surface.Refresh(false,light);
                    Check(!surface.Applied() && border.Background()==original && border.CornerRadius().TopLeft==4,"disable restores original values");
                    Check(border.ReadLocalValue(sx::Controls::Border::BorderBrushProperty())==sx::DependencyProperty::UnsetValue(),"unset restored");
                }
                surface.Refresh(true,false);
                sx::Media::SolidColorBrush newer(winrt::Windows::UI::Color{255,30,20,10});
                border.Background(newer);Check(border.Background()!=newer,"shell changes reapply immediately");
                surface.Restore();Check(border.Background()==newer,"latest shell value restored");
                sx::Data::Binding binding;binding.Source(original);
                border.SetBinding(sx::Controls::Border::BackgroundProperty(),binding);
                surface.Refresh(true,false);surface.Restore();
                auto expression=border.GetBindingExpression(sx::Controls::Border::BackgroundProperty());
                Check(expression && expression.ParentBinding()==binding,"background binding restored");
                surface.Refresh(true,true);
            }
            Check(border.GetBindingExpression(sx::Controls::Border::BackgroundProperty())!=nullptr,"destructor restores binding");
            sx::Controls::Border overlay;overlay.Name(L"AcrylicOverlay");
            {StartSurface surface(overlay);surface.Refresh(true,false);Check(overlay.Visibility()==sx::Visibility::Collapsed,"stock opaque overlay hidden");}
            Check(overlay.Visibility()==sx::Visibility::Visible,"overlay restored on shutdown");
            sx::Controls::Grid content;content.Width(640);content.Height(400);content.CornerRadius({8,8,8,8});
            auto visual=sx::Hosting::ElementCompositionPreview::GetElementVisual(content);
            auto originalClip=visual.Compositor().CreateInsetClip(1,2,3,4);visual.Clip(originalClip);
            {
                StartCorners corners(content,StartCornerRule::Content);corners.Refresh(true);
                Check(content.CornerRadius().TopLeft==19 && visual.Clip()==originalClip,"native radius changes without taking ownership of the shell compositor clip");
                corners.Refresh(false);Check(content.CornerRadius().TopLeft==8 && visual.Clip()==originalClip,"content radius restores and pre-existing compositor clip is untouched");
                corners.Refresh(true);auto newer=visual.Compositor().CreateInsetClip(2,3,4,5);visual.Clip(newer);corners.Refresh(true);corners.Restore();Check(visual.Clip()==newer,"later native clip remains untouched");
                sx::Data::Binding radiusBinding;radiusBinding.Source(winrt::box_value(sx::CornerRadius{7,7,7,7}));content.SetBinding(sx::Controls::Grid::CornerRadiusProperty(),radiusBinding);
                corners.Refresh(true);corners.Restore();Check(content.GetBindingExpression(sx::Controls::Grid::CornerRadiusProperty()).ParentBinding()==radiusBinding,"content corner binding restored");
            }
            sx::Controls::Border shadow;shadow.Name(L"StartDropShadow");shadow.CornerRadius({8,8,8,8});
            Check(StartCornerFor(shadow)==StartCornerRule::None,"unrelated shadow rejected");
            {StartCorners corners(shadow,StartCornerRule::Shadow);corners.Refresh(true);Check(shadow.CornerRadius().BottomRight==20,"native shadow matches outer acrylic radius");}
            Check(shadow.CornerRadius().BottomRight==8,"native shadow radius restored");
        }
        manager.Close();Pump();
        std::puts("PASS: real XAML Start surface theme transitions, tint/stroke, opaque overlay, shell changes, unset/binding/destructor restoration.");return 0;
    }catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx\n",static_cast<unsigned long>(e.code()));}
    catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}
    return 1;
}

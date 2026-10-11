// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_style.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void Pump() {MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        {
            sx::Controls::Border border; border.Name(L"AcrylicBorder");
            sx::Media::SolidColorBrush original(winrt::Windows::UI::Color{255,10,20,30});
            border.Background(original);border.CornerRadius({4,4,4,4});
            Check(!IsStartSurface(border),"unrelated named border is rejected");
            {
                StartSurface surface(border);
                for(unsigned cycle=0;cycle<3;++cycle) for(bool light:{false,true}) {
                    surface.Refresh(true,light);Check(surface.Applied(),"applied");
                    auto brush=border.Background().try_as<sx::Media::AcrylicBrush>();
                    Check(brush && brush.BackgroundSource()==sx::Media::AcrylicBackgroundSource::HostBackdrop,"desktop acrylic source");
                    Check(brush.TintOpacity()==184/255.0 && brush.TintLuminosityOpacity().Value()==184/255.0,"shared menu opacity");
                    const auto tint=brush.TintColor();Check(tint.R==(light?249:32),"shared menu tint");
                    const auto stroke=border.BorderBrush().as<sx::Media::SolidColorBrush>().Color();
                    Check(stroke.R==(light?210:69) && border.BorderThickness().Left==1 && border.CornerRadius().TopLeft==8,"menu stroke and radius");
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
        }
        manager.Close();Pump();
        std::puts("PASS: real XAML Start surface theme transitions, tint/stroke, opaque overlay, shell changes, unset/binding/destructor restoration.");return 0;
    }catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx\n",static_cast<unsigned long>(e.code()));}
    catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}
    return 1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/adaptive_contrast.hpp"
#include "../src/adaptive_bitmap.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
namespace x=winrt::Windows::UI::Xaml;namespace c=x::Controls;namespace m=x::Media;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void Pump(unsigned ms=50){const auto deadline=GetTickCount64()+ms;do{MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<deadline);}
bool owner=true;bool Alive(){return owner;}
m::Imaging::WriteableBitmap Bitmap(bool colored=false){m::Imaging::WriteableBitmap b(24,24);auto data=b.PixelBuffer().data();for(int y=0;y<24;++y)for(int z=0;z<24;++z){auto p=data+(y*24+z)*4;const int d=(y-12)*(y-12)+(z-12)*(z-12);p[0]=255;p[1]=colored?0:255;p[2]=255;p[3]=d>25 && d<100?255:0;}return b;}
int main(){try{winrt::init_apartment(winrt::apartment_type::single_threaded);auto manager=x::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    HWND host=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"jellyflower contrast test",WS_POPUP,100,100,400,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Check(host!=nullptr,"host");
    x::Hosting::DesktopWindowXamlSource source;auto native=source.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(host));HWND island{};winrt::check_hresult(native->get_WindowHandle(&island));SetWindowPos(island,nullptr,0,0,400,160,SWP_NOZORDER|SWP_SHOWWINDOW);ShowWindow(host,SW_SHOWNOACTIVATE);
    c::StackPanel root;root.Orientation(c::Orientation::Horizontal);root.Background(m::SolidColorBrush({255,32,32,32}));
    c::TextBlock glyph;glyph.Text(L"9:41 am");glyph.FontSize(16);glyph.Width(100);glyph.VerticalAlignment(x::VerticalAlignment::Center);
    m::SolidColorBrush white({255,255,255,255}),yellow({255,255,220,0});glyph.Foreground(white);root.Children().Append(glyph);
    c::Grid iconPanel;iconPanel.Width(48);iconPanel.Height(48);c::Image image;image.Width(24);image.Height(24);auto mono=Bitmap();image.Source(mono);iconPanel.Children().Append(image);root.Children().Append(iconPanel);
    c::Button button;button.Content(winrt::box_value(L"click"));int clicks=0;button.Click([&](auto&&,auto&&){++clicks;});root.Children().Append(button);source.Content(root);Pump(200);root.UpdateLayout();
    Check(acrylic::adaptive::Inspect(mono).monochrome,"transparent neutral bitmap accepted");Check(!acrylic::adaptive::Inspect(Bitmap(true)).monochrome,"colored bitmap rejected");
    {acrylic::AdaptiveView ink(glyph,Alive);ink.Refresh(true,true);Check(ink.Applied() && glyph.Foreground()!=white,"foreground replaced without text/layout edits");
     glyph.Foreground(yellow);Check(!ink.Applied() && glyph.Foreground()==yellow,"colored native status immediately restored");glyph.Foreground(white);Check(ink.Applied(),"new neutral shell write readapplied");
     white.Color({255,255,0,0});Check(!ink.Applied() && glyph.Foreground()==white,"mutable native brush color respected");white.Color({255,255,255,255});Check(ink.Applied(),"mutable neutral brush readapplied");
     ink.Refresh(false,true);Check(!ink.Applied() && glyph.Foreground()==white,"mode off restores latest foreground");
     x::Data::Binding binding;binding.Source(root);binding.Path(x::PropertyPath(L"Tag"));root.Tag(white);glyph.SetBinding(c::TextBlock::ForegroundProperty(),binding);ink.Refresh(true,true);Check(!ink.Applied(),"live status-color binding left native");root.Tag(yellow);Pump();Check(glyph.Foreground()==yellow,"bound warning color updates while pure remains active");ink.Restore();Check(glyph.GetBindingExpression(c::TextBlock::ForegroundProperty())!=nullptr,"binding retained as binding");
     glyph.ClearValue(c::TextBlock::ForegroundProperty());ink.Refresh(true,true);ink.Restore();Check(glyph.ReadLocalValue(c::TextBlock::ForegroundProperty())==x::DependencyProperty::UnsetValue(),"unset native property remains unset");}
    {acrylic::AdaptiveView ink(image,Alive);image.Opacity(.8);const double width=iconPanel.ActualWidth();ink.Refresh(true,true);Pump(250);ink.Refresh(true,true);Pump(50);
     std::printf("bitmap applied=%d error=%08x reads=%llu builds=%llu loaded=%d visible=%d size=%g,%g children=%u opacity=%g\n",ink.Applied(),unsigned(ink.Error()),ink.Stats().bitmapReads,ink.Stats().maskBuilds,image.IsLoaded(),image.XamlRoot().IsHostVisible(),image.ActualWidth(),image.ActualHeight(),iconPanel.Children().Size(),image.Opacity());Check(ink.Applied() && image.Opacity()==0 && iconPanel.Children().Size()==2,"bitmap mask loaded and native pixels hidden");
     Check(iconPanel.ActualWidth()==width && image.IsHitTestVisible(),"overlay does not alter original geometry/input");Check(!iconPanel.Children().GetAt(1).IsHitTestVisible(),"overlay never consumes input");
     const auto stableOverlay=iconPanel.Children().GetAt(1);const auto stableBuilds=ink.Stats().maskBuilds;
     image.Source(Bitmap());Check(ink.Applied() && image.Opacity()==0 && iconPanel.Children().Size()==2 && iconPanel.Children().GetAt(1)==stableOverlay,"identical replacement source must never expose native artwork or replace overlay");
     for(int i=0;i<20;++i){image.Source(Bitmap());ink.Refresh(true,true);Pump(5);Check(ink.Applied() && iconPanel.Children().GetAt(1)==stableOverlay,"duplicate source burst keeps one stable overlay");}
     Check(ink.Stats().maskBuilds==stableBuilds,"duplicate source burst performs no PNG decode/rebuild");
     image.Source(mono);
     image.Visibility(x::Visibility::Collapsed);ink.Refresh(true,true);Check(iconPanel.Children().GetAt(1).Visibility()==x::Visibility::Collapsed,"native icon hiding also hides replacement");image.Visibility(x::Visibility::Visible);iconPanel.UpdateLayout();ink.Refresh(true,true);Check(iconPanel.Children().GetAt(1).Visibility()==x::Visibility::Visible,"native icon reveal restores cached replacement");
     auto stats=ink.Stats();for(int i=0;i<30;++i)ink.Refresh(true,true);Check(ink.Stats().bitmapReads==stats.bitmapReads && ink.Stats().maskBuilds==stats.maskBuilds,"no repeated reads or PNG work inside throttle");
     Pump(260);ink.Refresh(true,true);Check(ink.Stats().maskBuilds==stats.maskBuilds,"unchanged bitmap reuses mask");stats=ink.Stats();for(int i=0;i<50;++i)ink.Refresh(true,false,GetTickCount64()+1000+i*250);Check(ink.Stats().bitmapReads==stats.bitmapReads,"hidden taskbar does no pixel reads");
     auto bytes=mono.PixelBuffer().data();for(int i=0;i<24*24;++i)if(bytes[i*4+3])bytes[i*4+1]=0;mono.Invalidate();Pump(260);ink.Refresh(true,true);std::printf("restored applied=%d opacity=%.17g children=%u mono=%d reads=%llu error=%08x\n",ink.Applied(),image.Opacity(),iconPanel.Children().Size(),acrylic::adaptive::Inspect(mono).monochrome,ink.Stats().bitmapReads,unsigned(ink.Error()));Check(!ink.Applied() && std::abs(image.Opacity()-.8)<.00001 && iconPanel.Children().Size()==1,"same bitmap becoming colored restores original");
     x::Data::Binding opacity;opacity.Source(iconPanel);opacity.Path(x::PropertyPath(L"Tag"));iconPanel.Tag(winrt::box_value(.6));image.SetBinding(x::UIElement::OpacityProperty(),opacity);image.Source(Bitmap());ink.Refresh(true,true);Check(!ink.Applied(),"bound bitmap opacity stays native");iconPanel.Tag(winrt::box_value(.4));Pump();Check(std::abs(image.Opacity()-.4)<.00001,"bound opacity continues updating");image.ClearValue(x::UIElement::OpacityProperty());image.Opacity(.8);
     image.Source(Bitmap());Pump(260);ink.Refresh(true,true);Pump(200);Check(ink.Applied(),"new monochrome source supported");
     image.SetBinding(x::UIElement::OpacityProperty(),opacity);Pump();Check(!ink.Applied() && image.GetBindingExpression(x::UIElement::OpacityProperty())!=nullptr,"new native opacity binding retires replacement immediately");image.ClearValue(x::UIElement::OpacityProperty());image.Opacity(.8);ink.Refresh(true,true);Pump(260);ink.Refresh(true,true);Pump(100);Check(ink.Applied(),"neutral unbound bitmap can adapt again");
     owner=false;ink.Refresh(true,true);Check(!ink.Applied() && std::abs(image.Opacity()-.8)<.00001 && iconPanel.Children().Size()==1,"owner death restores source opacity and removes overlay");owner=true;
     const auto masks=ink.Stats().maskBuilds;for(int i=0;i<20;++i)image.Source(Bitmap());Check(ink.Stats().maskBuilds<=masks+1,"rapid source swaps cannot bypass mask-build throttle");ink.Refresh(true,true);ink.Restore();Pump(200);Check(!ink.Applied() && iconPanel.Children().Size()==1,"late decode cannot apply after disable");}
    auto peer=x::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button);peer.GetPattern(x::Automation::Peers::PatternInterface::Invoke).as<x::Automation::Provider::IInvokeProvider>().Invoke();Pump();Check(clicks==1,"native button still works");
    source.Close();DestroyWindow(host);manager.Close();std::puts("PASS: glyph ownership/bindings/live status colors; bitmap masks/source mutation/cache/throttle/visibility/owner exit/cancel/input/layout.");return 0;
}catch(winrt::hresult_error const& e){std::fwprintf(stderr,L"HRESULT %08x %s\n",unsigned(e.code()),e.message().c_str());return 1;}catch(std::exception const& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}

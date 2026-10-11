// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_grid.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Layout(const sx::Controls::Grid& root){for(unsigned i=0;i<12;++i){root.UpdateLayout();Pump();Sleep(8);}}
winrt::hstring Supported(const sx::DependencyObject& object){auto f=object.try_as<sx::FrameworkElement>();if(f && f.Name()==L"MainContent")return L"StartMenu.StartHome";if(f && f.Name()==L"Category")return L"StartMenu.CategoryControl";return winrt::get_class_name(object);}
double X(const sx::FrameworkElement& item,const sx::UIElement& root){return item.TransformToVisual(root).TransformPoint({0,0}).X;}
int main(){try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);auto manager=sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerGridFixture";wc.lpfnWndProc=DefWindowProcW;RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower grid fixture",WS_POPUP,100,100,1100,700,nullptr,nullptr,wc.hInstance,nullptr);
    sx::Hosting::DesktopWindowXamlSource host;auto native=host.as<IDesktopWindowXamlSourceNative>();winrt::check_hresult(native->AttachToWindow(window));HWND child=nullptr;native->get_WindowHandle(&child);SetWindowPos(child,nullptr,0,0,1100,700,SWP_SHOWWINDOW);
    sx::Controls::Grid root;root.Name(L"MainContent");root.Width(832);host.Content(root);
    struct Variation{const wchar_t* name;double cell;double gap;int columns;bool compact,category;double reserved=0;};
    for(auto v:{Variation{L"AllAppsGrid",152,28,4,false,true},Variation{L"AllAppsGrid",160,20,4,false,false},
        Variation{L"AllAppsGrid",728,0,1,false,false},Variation{L"PinnedList",96,0,-1,false,false},Variation{L"PinnedList",96,0,-1,false,false,24},
        Variation{L"RecommendedList",236,20,3,true,false},Variation{L"RecommendedList",728,0,1,false,false}}) {
        sx::Controls::Grid parent;parent.Name(v.compact?L"TopLevelSuggestionsContainer":L"MoreSuggestionsRoot");
        if(v.compact)parent.Margin({12,0,12,0});
        const bool expanded=std::wstring_view(v.name)==L"RecommendedList" && !v.compact;
        if(expanded)parent.Margin({52,0,52,0});
        if(v.reserved){sx::Controls::ColumnDefinition main,indicator;indicator.Width({v.reserved,sx::GridUnitType::Pixel});parent.ColumnDefinitions().Append(main);parent.ColumnDefinitions().Append(indicator);}
        root.Children().Append(parent);
        auto view=sx::Markup::XamlReader::Load(L"<GridView xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"><GridView.ItemsPanel><ItemsPanelTemplate><ItemsWrapGrid Orientation=\"Horizontal\" Margin=\"64,0,0,14\"/></ItemsPanelTemplate></GridView.ItemsPanel></GridView>").as<sx::Controls::GridView>();
        const double originalWidth=expanded?728:v.compact?820:832;
        view.Name(v.name);view.Width(originalWidth);view.VerticalAlignment(sx::VerticalAlignment::Top);
        std::vector<sx::Controls::GridViewItem> items;std::vector<sx::Controls::ContentControl> cards;
        const auto count=v.columns==1?3:16;
        for(int i=0;i<count;++i) {
            sx::Controls::GridViewItem item;item.MinWidth(v.cell);item.MaxWidth(v.cell);item.Height(70);item.Margin({0,0,v.gap,0});item.Padding({12,0,12,0});
            sx::Controls::ContentControl card;card.Name(v.category?L"Category":L"Card");card.Width(v.cell);card.Content(winrt::box_value(L"fixture"));item.Content(card);
            items.push_back(item);cards.push_back(card);view.Items().Append(item);
        }
        parent.Children().Append(view);Layout(root);auto panel=view.ItemsPanelRoot().as<sx::Controls::ItemsWrapGrid>();panel.MaximumRowsOrColumns(v.columns);
        Check(!IsStartGrid(view) && IsStartGrid(view,Supported),"only recognized Start ancestors accepted");
        const auto originalPanel=panel.Margin();const auto originalItem=items[0].Margin();
        {
            StartGrid style(view,Supported);style.Refresh(true);
            Check(panel.Margin().Left==originalPanel.Left,"grid changes are deferred outside the current layout pass");
            style.Restore();style.Refresh(true); // A cancelled queued tick must not affect the next generation.
            for(double width:{832.0,751.333333,624.0,953.666667}) {
                root.Width(width);Layout(root);
                const auto columns=StartColumns(root.ActualWidth()-2*std::max(12.0,v.reserved),v.cell+v.gap,v.columns);
                Check(columns>0,"responsive columns present");
                const auto& first=items[0];const auto& last=items[columns-1];
                const double left=X(first,root),right=root.ActualWidth()-X(last,root)-last.ActualWidth();
                std::printf("grid %ls %s width=%.3f columns=%u left=%.3f right=%.3f cell=%.3f\n",v.name,v.category?"category":"rows",width,columns,left,right,panel.ItemWidth());
                Check(std::abs(left-right)<1.5,"first and last cell have balanced outer gaps at fractional widths");
                for(unsigned i=1;i<columns;++i)Check(std::abs((X(items[i],root)-X(items[i-1],root))-panel.ItemWidth())<1.5,"equal horizontal spacing across a complete row");
                Check(X(items[columns],root)<X(first,root)+1.5,"items wrap to the next row in native order");
                if(v.category)Check(std::abs(cards[0].ActualWidth()-first.ActualWidth()+24)<1.5,"category content fills its cell with native inner padding");
                Check(first.Padding().Left==12 && first.Height()==70,"native inner padding and vertical sizing retained");
            }
            const unsigned idleStart=style.LayoutPasses();
            unsigned idleLayouts=0;auto idleToken=view.LayoutUpdated([&](auto&&,auto&&){++idleLayouts;});
            for(unsigned i=0;i<40;++i){Pump();Sleep(2);}
            view.LayoutUpdated(idleToken);std::printf("idle: native events=%u grid passes=%u\n",idleLayouts,style.LayoutPasses()-idleStart);
            Check(style.LayoutPasses()==idleStart,"stable grid does not schedule repeated idle layout work");
            items[0].MinWidth(v.cell+1);items[0].Margin({2,3,v.gap+2,9});Layout(root);
            style.Restore();Check(items[0].MinWidth()==v.cell+1 && items[0].Margin().Top==3,"latest shell changes restore");
        }
        Check(view.Width()==originalWidth && panel.Margin().Left==originalPanel.Left && std::isnan(panel.ItemWidth()),"view/panel width and asymmetric native margins restore");
        Check(items[1].MaxWidth()==v.cell && items[1].Margin().Right==originalItem.Right && cards[0].Width()==v.cell,"fixed container/content widths restore");
        root.Children().Clear();root.Width(832);Layout(root);
    }
    Check(StartColumns(808,180,4)==4 && StartColumns(600,180,4)==3 && !StartColumns(0,180,4),"column bounds handle narrow/empty layouts");
    host.Content(nullptr);host.Close();DestroyWindow(window);manager.Close();Pump();
    std::puts("PASS: category/name-grid/name-list/pinned/compact and expanded Recommended layouts; fractional resize, balanced cells, native vertical order/padding and latest property restoration.");return 0;
}catch(const winrt::hresult_error& e){std::printf("FAIL HRESULT %08lx %s\n",static_cast<unsigned long>(e.code()),winrt::to_string(e.message()).c_str());}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());}return 1;}

// SPDX-License-Identifier: GPL-3.0-or-later
// Start surface names verified against Windhawk's Windows 11 Start Menu Styler.
// Uses the same documented XAML diagnostics TAP pattern as our taskbar bridge.
#include <unknwn.h>
#include "start_shared.hpp"
#include "start_style.hpp"
#include "start_search.hpp"
#include "search_input.hpp"
#include "start_spacing.hpp"
#include "start_grid.hpp"
#include "start_header.hpp"
#include "start_pinned.hpp"
#include "start_corners.hpp"
#include "start_user.hpp"
#include <shobjidl.h>
#include <xamlOM.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <mutex>

using namespace acrylic;
namespace {
Handle mapping;
StartState* state=nullptr;
HMODULE module=nullptr;
void Result(HRESULT result) noexcept { if(state) InterlockedExchange(&state->result,result); }
struct Owner {
    std::mutex mutex;
    Handle process;
    bool Alive() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if(!state || Read(state->magic)!=StartMagic || !Read(state->running)) return false;
        // One shared owner for all XAML threads consumes each transferred
        // handle once. Rebinding never closes a newer owner's handle because
        // epoch and handle publication happened on different threads.
        if(const auto next=InterlockedExchange64(&state->ownerHandle,0))process.reset(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(next)));
        return process && WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT;
    }
};
Owner owner;
struct Surface {
    StartSurface view;
    bool counted=false;
    explicit Surface(const sx::Controls::Border& border):view(border) {}
    void Refresh() noexcept {
        try { view.Refresh(owner.Alive() && Read(state->enabled),state && Read(state->light)); }
        catch(...) { Result(winrt::to_hresult()); try { view.Restore(); } catch(...) {} }
        if(state && counted!=view.Applied()) {
            counted=view.Applied();
            if(counted) InterlockedIncrement(&state->count); else InterlockedDecrement(&state->count);
        }
    }
    ~Surface() { if(counted && state) InterlockedDecrement(&state->count); }
};
template<class View> struct SearchStyle {
    View view;
    bool counted=false;
    explicit SearchStyle(const sx::Controls::Control& control):view(control) {}
    void Refresh() noexcept {
        try { view.Refresh(owner.Alive() && Read(state->enabled),state && Read(state->light),state && state->placeholder[0]?state->placeholder:L"Search"); }
        catch(...) { Result(winrt::to_hresult()); view.Restore(); }
        if(state && counted!=view.Applied()) {
            counted=view.Applied();
            if(counted) InterlockedIncrement(&state->searches); else InterlockedDecrement(&state->searches);
        }
    }
    ~SearchStyle() { if(counted && state) InterlockedDecrement(&state->searches); }
};
template<class View> auto& Searches() {
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<SearchStyle<View>>>;
    return *items;
}
struct Spacing {
    StartSpacing view;
    bool counted=false;
    Spacing(const sx::FrameworkElement& element,StartSpacingRule rule):view(element,rule) {}
    void Refresh() noexcept {
        try {view.Refresh(owner.Alive() && Read(state->enabled));}
        catch(...) {Result(winrt::to_hresult());view.Restore();}
        if(state && counted!=view.Applied()) {
            counted=view.Applied();
            if(counted) InterlockedIncrement(&state->spacing);else InterlockedDecrement(&state->spacing);
        }
    }
    ~Spacing() {if(counted && state) InterlockedDecrement(&state->spacing);}
};
auto& Spacings() {
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<Spacing>>;
    return *items;
}
template<class View> struct LayoutStyle {
    View view;bool counted=false;
    unsigned lastPasses=0;
    template<class... Args> explicit LayoutStyle(Args&&... args):view(std::forward<Args>(args)...) {}
    void Refresh() noexcept {
        try {view.Refresh(owner.Alive() && Read(state->enabled));}
        catch(...) {Result(winrt::to_hresult());view.Restore();}
        if constexpr(std::is_same_v<View,StartGrid>) {
            if(state){auto total=view.Name()==L"PinnedList"?&state->pinnedPasses:view.Name()==L"RecommendedList"?&state->recommendedPasses:&state->gridPasses;
                const auto passes=view.LayoutPasses();InterlockedExchangeAdd(total,static_cast<LONG>(passes-lastPasses));lastPasses=passes;}
        }
        if(state && counted!=view.Applied()) {
            counted=view.Applied();if(counted)InterlockedIncrement(&state->spacing);else InterlockedDecrement(&state->spacing);
        }
    }
    ~LayoutStyle(){if(counted && state)InterlockedDecrement(&state->spacing);}
};
template<class View> auto& Layouts() {
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<LayoutStyle<View>>>;
    return *items;
}
auto& PendingGrids() {
    thread_local auto* items=new DeferredStartGrids;
    return *items;
}
void DiscoverGrids() {
    // React can publish a replacement GridView before its Start ancestors are
    // connected. Keep recognized names pending until a later template/layout
    // event establishes the guarded shell context.
    PendingGrids().Discover([](auto handle,const auto& view) {
        if(!Layouts<StartGrid>().count(handle)) {
            auto style=std::make_shared<LayoutStyle<StartGrid>>(view);
            Layouts<StartGrid>().emplace(handle,style);style->Refresh();
        }
    });
}
auto& Surfaces() {
    // XAML objects must be released on their UI thread, never in DLL teardown.
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<Surface>>;
    return *items;
}
void Refresh() noexcept {
    try {
        DiscoverGrids();
        std::vector<std::shared_ptr<Surface>> snapshot;
        for(const auto& item:Surfaces()) snapshot.push_back(item.second);
        for(const auto& item:snapshot) item->Refresh();
        std::vector<std::shared_ptr<SearchStyle<StartSearch>>> searches;
        for(const auto& item:Searches<StartSearch>()) searches.push_back(item.second);
        for(const auto& item:searches) item->Refresh();
        std::vector<std::shared_ptr<SearchStyle<SearchInput>>> inputs;
        for(const auto& item:Searches<SearchInput>()) inputs.push_back(item.second);
        for(const auto& item:inputs) item->Refresh();
        std::vector<std::shared_ptr<Spacing>> spacing;
        for(const auto& item:Spacings()) spacing.push_back(item.second);
        for(const auto& item:spacing) item->Refresh();
        std::vector<std::shared_ptr<LayoutStyle<StartGrid>>> grids;
        for(const auto& item:Layouts<StartGrid>())grids.push_back(item.second);
        for(const auto& item:grids)item->Refresh();
        std::vector<std::shared_ptr<LayoutStyle<StartHeader>>> headers;
        for(const auto& item:Layouts<StartHeader>())headers.push_back(item.second);
        for(const auto& item:headers)item->Refresh();
        std::vector<std::shared_ptr<LayoutStyle<StartPinned>>> pins;
        for(const auto& item:Layouts<StartPinned>())pins.push_back(item.second);
        for(const auto& item:pins)item->Refresh();
        std::vector<std::shared_ptr<LayoutStyle<StartCorners>>> corners;
        for(const auto& item:Layouts<StartCorners>())corners.push_back(item.second);
        for(const auto& item:corners)item->Refresh();
        std::vector<std::shared_ptr<LayoutStyle<StartUser>>> users;
        for(const auto& item:Layouts<StartUser>())users.push_back(item.second);
        for(const auto& item:users)item->Refresh();
    } catch(...) { Result(winrt::to_hresult()); }
}
LRESULT CALLBACK Notify(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_TIMER || message==StartRefreshMessage) { Refresh(); return 0; }
    return DefWindowProcW(window,message,w,l);
}
void EnsureReceiver() {
    thread_local HWND receiver=nullptr;
    if(receiver) return;
    WNDCLASSW wc{};wc.hInstance=module;wc.lpszClassName=StartNotifyClass;wc.lpfnWndProc=Notify;
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) winrt::throw_last_error();
    receiver=CreateWindowExW(0,StartNotifyClass,nullptr,0,0,0,0,0,HWND_MESSAGE,nullptr,module,nullptr);
    if(!receiver || !SetTimer(receiver,1,1000,nullptr)) winrt::throw_last_error();
}
struct Watcher:winrt::implements<Watcher,IVisualTreeServiceCallback2,winrt::non_agile> {
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    explicit Watcher(IUnknown* site) { winrt::check_hresult(site->QueryInterface(__uuidof(IXamlDiagnostics),diagnostics.put_void())); }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation,VisualElement element,VisualMutationType mutation) noexcept override {
        try {
            if(mutation==Remove) { Surfaces().erase(element.Handle); Searches<StartSearch>().erase(element.Handle);Searches<SearchInput>().erase(element.Handle); Spacings().erase(element.Handle); PendingGrids().Remove(element.Handle);Layouts<StartGrid>().erase(element.Handle);Layouts<StartHeader>().erase(element.Handle);Layouts<StartCorners>().erase(element.Handle);Layouts<StartPinned>().erase(element.Handle);Layouts<StartUser>().erase(element.Handle);return S_OK; }
            // Only Handle is valid for Remove. Add strings use the same BSTR
            // ownership contract as the taskbar diagnostics callback.
            struct Strings { VisualElement& e; ~Strings(){SysFreeString(e.Type);SysFreeString(e.Name);} } strings{element};
            if(mutation!=Add || !element.Type) return S_OK;
            const bool search=std::wstring_view(element.Type)==L"StartMenu.SearchBoxToggleButton" || std::wstring_view(element.Type)==L"StartDocked.SearchBoxToggleButton";
            const bool input=std::wstring_view(element.Type)==L"Cortana.UI.Views.RichSearchBoxControl" || std::wstring_view(element.Type)==L"Cortana.UI.Views.RichComposerBoxControl";
            const bool surface=element.Name && std::wstring_view(element.Type)==L"Windows.UI.Xaml.Controls.Border"
                && (std::wstring_view(element.Name)==L"AcrylicBorder" || std::wstring_view(element.Name)==L"AcrylicOverlay");
            const std::wstring_view type(element.Type);
            const bool user=element.Name && std::wstring_view(element.Name)==L"UserTileButton" && (type==L"StartMenu.NavigationPaneButton" || type==L"StartDocked.NavigationPaneButton");
            const bool pinned=type==L"StartMenu.PinnedListTile" || type==L"StartMenu.PinnedListCollapsedFolder" || type==L"StartDocked.PinnedListTile" || type==L"StartDocked.PinnedListCollapsedFolder";
            const bool grid=type==L"Windows.UI.Xaml.Controls.GridView";
            const bool corner=element.Name && (type==L"Windows.UI.Xaml.Controls.Grid" || type==L"Windows.UI.Xaml.Controls.Border");
            const bool spacing=type==L"Windows.UI.Xaml.Controls.ItemsWrapGrid" || type==L"Windows.UI.Xaml.Controls.Button"
                || (element.Name && (type==L"Windows.UI.Xaml.Controls.TextBlock" || type==L"Windows.UI.Xaml.Controls.Grid" || type==L"Microsoft.UI.Xaml.Controls.DropDownButton"));
            if(!search && !input && !surface && !spacing && !grid && !corner && !pinned && !user) return S_OK;
            winrt::Windows::Foundation::IInspectable object{nullptr};
            winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
            if(user) {
                auto button=object.try_as<sx::Controls::Control>();
                if(IsStartUserButton(button) && !Layouts<StartUser>().count(element.Handle)) {
                    EnsureReceiver();auto item=std::make_shared<LayoutStyle<StartUser>>(button);Layouts<StartUser>().emplace(element.Handle,item);item->Refresh();
                }
            }
            if(pinned) {
                auto tile=object.try_as<sx::FrameworkElement>();
                if(IsStartPinnedTile(tile) && !Layouts<StartPinned>().count(element.Handle)) {
                    EnsureReceiver();auto item=std::make_shared<LayoutStyle<StartPinned>>(tile);Layouts<StartPinned>().emplace(element.Handle,item);item->Refresh();
                }
            }
            if(grid) {
                auto view=object.try_as<sx::Controls::GridView>();
                if(view && (view.Name()==L"RecommendedList" || view.Name()==L"AllAppsGrid" || view.Name()==L"PinnedList") && !Layouts<StartGrid>().count(element.Handle)) {
                    EnsureReceiver();PendingGrids().Add(element.Handle,view);DiscoverGrids();
                }
            }
            if(corner) {
                auto framework=object.try_as<sx::FrameworkElement>();const auto rule=StartCornerFor(framework);
                if(rule!=StartCornerRule::None && !Layouts<StartCorners>().count(element.Handle)) {
                    EnsureReceiver();auto item=std::make_shared<LayoutStyle<StartCorners>>(framework,rule);Layouts<StartCorners>().emplace(element.Handle,item);item->Refresh();
                }
            }
            if(spacing) {
                const auto framework=object.try_as<sx::FrameworkElement>();
                const auto rule=StartSpacingFor(framework);
                if(rule==StartSpacingRule::SectionTop && framework.Name()==L"TopLevelHeader" && !Layouts<StartHeader>().count(element.Handle)) {
                    EnsureReceiver();auto item=std::make_shared<LayoutStyle<StartHeader>>(framework);Layouts<StartHeader>().emplace(element.Handle,item);item->Refresh();
                }
                if(rule!=StartSpacingRule::None && !Spacings().count(element.Handle)) {
                    EnsureReceiver();const auto item=std::make_shared<Spacing>(framework,rule);
                    Spacings().emplace(element.Handle,item);item->Refresh();
                }
            }
            if(pinned || type==L"Windows.UI.Xaml.Controls.ItemsWrapGrid" || (element.Name &&
                (std::wstring_view(element.Name)==L"RootGrid" || std::wstring_view(element.Name)==L"DisplayName"
                || std::wstring_view(element.Name)==L"LogoContainer" || std::wstring_view(element.Name)==L"LogosContainer"
                || std::wstring_view(element.Name)==L"SeeAllButton"))) {
                DiscoverGrids();
                auto ancestor=object.try_as<sx::DependencyObject>();
                for(unsigned depth=0;ancestor && depth<64;++depth) {
                    for(const auto& item:Layouts<StartGrid>())if(item.second->view.Element()==ancestor)item.second->view.Invalidate();
                    if(element.Name && std::wstring_view(element.Name)==L"DisplayName")
                        for(const auto& item:Layouts<StartPinned>())if(item.second->view.Element()==ancestor)item.second->Refresh();
                    ancestor=sx::Media::VisualTreeHelper::GetParent(ancestor);
                }
            }
            if(input){
                auto control=object.try_as<sx::Controls::Control>();
                if(IsSearchInput(control) && !Searches<SearchInput>().count(element.Handle)){
                    EnsureReceiver();auto item=std::make_shared<SearchStyle<SearchInput>>(control);Searches<SearchInput>().emplace(element.Handle,item);item->Refresh();
                }
                return S_OK;
            }
            if(!search && !surface) return S_OK;
            if(search) {
                const auto control=object.try_as<sx::Controls::Control>();
                if(!IsStartSearch(control) || Searches<StartSearch>().count(element.Handle)) return S_OK;
                EnsureReceiver();
                const auto item=std::make_shared<SearchStyle<StartSearch>>(control);Searches<StartSearch>().emplace(element.Handle,item);item->Refresh();
                return S_OK;
            }
            const auto border=object.try_as<sx::Controls::Border>();
            if(!IsStartSurface(border) || Surfaces().count(element.Handle)) return S_OK;
            EnsureReceiver();
            const auto item=std::make_shared<Surface>(border);
            Surfaces().emplace(element.Handle,item);item->Refresh();
        } catch(...) { Result(winrt::to_hresult()); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle,VisualElementState,LPCWSTR) noexcept override { return S_OK; }
};
struct Site:winrt::implements<Site,IObjectWithSite,winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;
    winrt::com_ptr<Watcher> watcher;
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override {
        try {
            if(!value) { site=nullptr;return S_OK; }
            if(watcher) return E_UNEXPECTED;
            if(!state) {
                winrt::com_ptr<IXamlDiagnostics> diagnostics;winrt::check_hresult(value->QueryInterface(__uuidof(IXamlDiagnostics),diagnostics.put_void()));
                BSTR data=nullptr;winrt::check_hresult(diagnostics->GetInitializationData(&data));
                struct String {BSTR value;~String(){SysFreeString(value);}} string{data};
                unsigned long long handle=0;int end=0;
                if(!data || swscanf_s(data,L"JFStart16 %llx%n",&handle,&end)!=1 || data[end] || !handle)return E_INVALIDARG;
                // The controller owns the supplied handle until it exits.
                // Keep a private copy so failed/late initialization cannot
                // double-close a transferred handle or leak one per retry.
                HANDLE local=nullptr;
                if(!DuplicateHandle(GetCurrentProcess(),reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),
                    GetCurrentProcess(),&local,0,FALSE,DUPLICATE_SAME_ACCESS))winrt::throw_last_error();
                mapping.reset(local);
                state=static_cast<StartState*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(StartState)));
                if(state)InterlockedExchange(&state->mappingClaimed,1);
            }
            if(!state || Read(state->magic)!=StartMagic || !Read(state->running)) return E_ABORT;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&Result),&module)) winrt::throw_last_error();
            site.copy_from(value);watcher=winrt::make_self<Watcher>(value);
            std::thread([strong=watcher] {
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    winrt::check_hresult(strong->diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(strong.get()));
                    InterlockedExchange(&state->attached,1);winrt::uninit_apartment();
                } catch(...) { Result(winrt::to_hresult());InterlockedExchange(&state->attached,-1); }
            }).detach();return S_OK;
        } catch(...) { Result(winrt::to_hresult());return winrt::to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void** output) noexcept override {
        if(!output) return E_POINTER;*output=nullptr;return site ? site->QueryInterface(iid,output):E_FAIL;
    }
};
struct Factory:winrt::implements<Factory,IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** output) noexcept override {
        if(!output) return E_POINTER;*output=nullptr;if(outer) return CLASS_E_NOAGGREGATION;
        try { return winrt::make_self<Site>()->QueryInterface(iid,output); } catch(...) {return winrt::to_hresult();}
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** output) {
    if(!output) return E_POINTER;*output=nullptr;
    if(!IsEqualCLSID(clsid,StartTapClass)) return CLASS_E_CLASSNOTAVAILABLE;
    try {return winrt::make_self<Factory>()->QueryInterface(iid,output);}catch(...){return winrt::to_hresult();}
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {return S_FALSE;}

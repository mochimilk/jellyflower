// SPDX-License-Identifier: GPL-3.0-or-later
// Start surface names verified against Windhawk's Windows 11 Start Menu Styler.
// Uses the same documented XAML diagnostics TAP pattern as our taskbar bridge.
#include <unknwn.h>
#include "start_shared.hpp"
#include "start_style.hpp"
#include "start_search.hpp"
#include <shobjidl.h>
#include <xamlOM.h>
#include <memory>
#include <thread>
#include <unordered_map>

using namespace acrylic;
namespace {
Handle mapping;
StartState* state=nullptr;
HMODULE module=nullptr;
void Result(HRESULT result) noexcept { if(state) InterlockedExchange(&state->result,result); }
struct Owner {
    LONG epoch=-1;
    Handle process;
    bool Alive() noexcept {
        if(!state || Read(state->magic)!=StartMagic || !Read(state->running)) return false;
        if(epoch!=Read(state->epoch) || !process) {
            epoch=Read(state->epoch);
            process.reset(OpenProcess(SYNCHRONIZE,FALSE,Read(state->controllerPid)));
        }
        return process && WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT;
    }
};
thread_local Owner owner;
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
struct Search {
    StartSearch view;
    bool counted=false;
    explicit Search(const sx::Controls::Control& control):view(control) {}
    void Refresh() noexcept {
        try { view.Refresh(owner.Alive() && Read(state->enabled),state && Read(state->light)); }
        catch(...) { Result(winrt::to_hresult()); view.Restore(); }
        if(state && counted!=view.Applied()) {
            counted=view.Applied();
            if(counted) InterlockedIncrement(&state->searches); else InterlockedDecrement(&state->searches);
        }
    }
    ~Search() { if(counted && state) InterlockedDecrement(&state->searches); }
};
auto& Searches() {
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<Search>>;
    return *items;
}
auto& Surfaces() {
    // XAML objects must be released on their UI thread, never in DLL teardown.
    thread_local auto* items=new std::unordered_map<InstanceHandle,std::shared_ptr<Surface>>;
    return *items;
}
void Refresh() noexcept {
    try {
        std::vector<std::shared_ptr<Surface>> snapshot;
        for(const auto& item:Surfaces()) snapshot.push_back(item.second);
        for(const auto& item:snapshot) item->Refresh();
        std::vector<std::shared_ptr<Search>> searches;
        for(const auto& item:Searches()) searches.push_back(item.second);
        for(const auto& item:searches) item->Refresh();
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
            if(mutation==Remove) { Surfaces().erase(element.Handle); Searches().erase(element.Handle); return S_OK; }
            // Only Handle is valid for Remove. Add strings use the same BSTR
            // ownership contract as the taskbar diagnostics callback.
            struct Strings { VisualElement& e; ~Strings(){SysFreeString(e.Type);SysFreeString(e.Name);} } strings{element};
            if(mutation!=Add || !element.Type) return S_OK;
            const bool search=std::wstring_view(element.Type)==L"StartMenu.SearchBoxToggleButton" || std::wstring_view(element.Type)==L"StartDocked.SearchBoxToggleButton";
            const bool surface=element.Name && std::wstring_view(element.Type)==L"Windows.UI.Xaml.Controls.Border"
                && (std::wstring_view(element.Name)==L"AcrylicBorder" || std::wstring_view(element.Name)==L"AcrylicOverlay");
            if(!search && !surface) return S_OK;
            winrt::Windows::Foundation::IInspectable object{nullptr};
            winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
            if(search) {
                const auto control=object.try_as<sx::Controls::Control>();
                if(!IsStartSearch(control) || Searches().count(element.Handle)) return S_OK;
                EnsureReceiver();
                const auto item=std::make_shared<Search>(control);Searches().emplace(element.Handle,item);item->Refresh();
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
                mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,StartMappingName(GetCurrentProcessId()).c_str()));
                if(!mapping) winrt::throw_last_error();
                state=static_cast<StartState*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(StartState)));
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

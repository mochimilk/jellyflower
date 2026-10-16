// SPDX-License-Identifier: GPL-3.0-or-later
#include "adaptive_session.hpp"
#include "adaptive_contrast.hpp"
#include <wtsapi32.h>
#include <dwmapi.h>
#include <unordered_map>
#include <vector>

namespace acrylic {namespace {
constexpr wchar_t NotifyClass[]=L"jellyflower.Contrast.v1";
constexpr UINT RefreshMessage=WM_APP+103;
bool TaskbarWindow(HWND window){wchar_t name[64]{};GetClassNameW(window,name,64);return !wcscmp(name,L"Shell_TrayWnd") || !wcscmp(name,L"Shell_SecondaryTrayWnd");}
bool TaskbarShown(){
    bool shown=false;EnumWindows([](HWND window,LPARAM context)->BOOL{
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId() || !TaskbarWindow(window) || !IsWindowVisible(window))return TRUE;
        RECT bounds{},intersection{};MONITORINFO info{sizeof(info)};DWORD cloaked=0;
        if(!GetWindowRect(window,&bounds) || !GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info) || FAILED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) || cloaked)return TRUE;
        // Auto-hide leaves a thin activation strip. It is not a visible tray.
        if(IntersectRect(&intersection,&bounds,&info.rcMonitor) && intersection.bottom-intersection.top>=(bounds.bottom-bounds.top)/2 && intersection.bottom-intersection.top>4){*reinterpret_cast<bool*>(context)=true;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&shown));return shown;
}
struct Record{winrt::Windows::UI::Xaml::DependencyObject object{nullptr};std::unique_ptr<AdaptiveView> view;};
struct Session {
    std::unordered_map<unsigned long long,std::shared_ptr<Record>> records;
    HWND window=nullptr;SharedState* state=nullptr;bool(*alive)()=nullptr;
    bool locked=false,refreshing=false,bitmapTimer=false,heartbeat=false;
    LONG glyphs=0,bitmaps=0;
    HWINEVENTHOOK hook=nullptr;
    void Refresh()noexcept;
};
Session& Current(){thread_local auto* session=new Session;return *session;}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    auto& session=Current();
    if(message==WM_WTSSESSION_CHANGE){if(wParam==WTS_SESSION_LOCK)session.locked=true;else if(wParam==WTS_SESSION_UNLOCK)session.locked=false;session.Refresh();return 0;}
    if(message==RefreshMessage || message==WM_TIMER){session.Refresh();return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
void CALLBACK TaskbarEvent(HWINEVENTHOOK,DWORD,HWND window,LONG object,LONG child,DWORD,DWORD){
    auto& session=Current();if(object==OBJID_WINDOW && child==CHILDID_SELF && TaskbarWindow(window) && session.window)PostMessageW(session.window,RefreshMessage,0,0);
}
void Session::Refresh()noexcept{
    if(refreshing || !state || !alive)return;refreshing=true;
    try{
        const bool active=alive() && DecodeAcrylicMode(Read(state->active))==AcrylicMode::Pure;
        HIGHCONTRASTW contrast{sizeof(contrast)};const bool high=!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) || (contrast.dwFlags&HCF_HIGHCONTRASTON);
        const bool allowed=active && !high;
        const bool visible=allowed && !locked && TaskbarShown();
        bool bitmapCandidate=false;LONG nextGlyphs=0,nextBitmaps=0;HRESULT result=S_OK;
        std::vector<std::shared_ptr<Record>> snapshot;for(auto const& item:records)snapshot.push_back(item.second);
        for(auto const& item:snapshot){
            if(allowed && !item->view)try{item->view=std::make_unique<AdaptiveView>(item->object,alive);}catch(...){result=winrt::to_hresult();}
            if(!item->view)continue;
            item->view->Refresh(allowed,visible);
            bitmapCandidate|=item->view->IsBitmap();
            if(item->view->Applied()){if(item->view->IsBitmap())++nextBitmaps;else ++nextGlyphs;}
            if(FAILED(item->view->Error()))result=item->view->Error();
        }
        InterlockedExchangeAdd(&state->adaptiveGlyphs,nextGlyphs-glyphs);glyphs=nextGlyphs;
        InterlockedExchangeAdd(&state->adaptiveBitmaps,nextBitmaps-bitmaps);bitmaps=nextBitmaps;
        InterlockedExchange(&state->adaptiveResult,result);
        const bool needed=visible && bitmapCandidate;
        if(needed!=bitmapTimer && window){if(needed)bitmapTimer=SetTimer(window,1,250,nullptr)!=0;else{KillTimer(window,1);bitmapTimer=false;}}
        if(allowed!=heartbeat && window){if(allowed)heartbeat=SetTimer(window,2,2000,nullptr)!=0;else{KillTimer(window,2);heartbeat=false;}}
        if(allowed && !hook)hook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_LOCATIONCHANGE,nullptr,TaskbarEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
        if(!allowed && hook){UnhookWinEvent(hook);hook=nullptr;}
    }catch(...){InterlockedExchange(&state->adaptiveResult,winrt::to_hresult());}
    refreshing=false;
}
}
void TrackAdaptive(unsigned long long handle,winrt::Windows::UI::Xaml::DependencyObject const& object){
    if(!IsAdaptiveTrayTarget(object))return;
    auto& session=Current();if(session.records.find(handle)==session.records.end()){auto record=std::make_shared<Record>();record->object=object;session.records.emplace(handle,std::move(record));}
    session.Refresh();
}
void RemoveAdaptive(unsigned long long handle){
    auto& session=Current();const auto found=session.records.find(handle);if(found==session.records.end())return;
    const auto strong=found->second;session.records.erase(found);if(strong->view)strong->view->Restore();session.Refresh();
}
void RefreshAdaptive(HINSTANCE module,SharedState* state,bool(*alive)())noexcept{
    auto& session=Current();session.state=state;session.alive=alive;
    // Default system mode tracks lightweight references only, with no bitmap
    // inspection, shader creation, extra timer, or foreground color changes.
    if(!session.window && state && DecodeAcrylicMode(Read(state->active))==AcrylicMode::Pure){
        WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=Proc;wc.lpszClassName=NotifyClass;
        if(RegisterClassW(&wc) || GetLastError()==ERROR_CLASS_ALREADY_EXISTS)session.window=CreateWindowExW(0,NotifyClass,nullptr,0,0,0,0,0,HWND_MESSAGE,nullptr,module,nullptr);
        if(session.window){
            WTSRegisterSessionNotification(session.window,NOTIFY_FOR_THIS_SESSION);
            LPWSTR buffer=nullptr;DWORD bytes=0;
            if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSSessionInfoEx,&buffer,&bytes)){
                if(bytes>=sizeof(WTSINFOEXW)){const auto info=reinterpret_cast<WTSINFOEXW*>(buffer);if(info->Level==1)session.locked=info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_LOCK;}
                WTSFreeMemory(buffer);
            }
            // Refresh arms one shared liveness timer only while pure is active.
        }
    }
    session.Refresh();
}
}

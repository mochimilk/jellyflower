// SPDX-License-Identifier: GPL-3.0-or-later
#include "start_session.hpp"
#include "start_access.hpp"
#include <tlhelp32.h>
#include <xamlOM.h>
#include <string>

namespace acrylic {
namespace {
DWORD StartPid(ShellHost host) {
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    wchar_t windows[32768]{};
    if(!GetWindowsDirectoryW(windows,32768)) return 0;
    const bool search=host==ShellHost::Search;
    const auto expected=std::wstring(windows)+(search?L"\\SystemApps\\MicrosoftWindows.Client.CBS_cw5n1h2txyewy\\SearchHost.exe":L"\\SystemApps\\Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy\\StartMenuExperienceHost.exe");
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    PROCESSENTRY32W entry{sizeof(entry)};
    if(!snapshot || !Process32FirstW(snapshot.get(),&entry)) return 0;
    do {
        if(_wcsicmp(entry.szExeFile,search?L"SearchHost.exe":L"StartMenuExperienceHost.exe")) continue;
        DWORD targetSession=0;
        if(!ProcessIdToSessionId(entry.th32ProcessID,&targetSession) || session!=targetSession) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
        wchar_t path[32768]{};DWORD length=32768;
        if(process && QueryFullProcessImageNameW(process.get(),0,path,&length) && !_wcsicmp(path,expected.c_str())) return entry.th32ProcessID;
    }while(Process32NextW(snapshot.get(),&entry));
    return 0;
}
bool EffectsEnabled() {
    HIGHCONTRASTW high{sizeof(high)};
    if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(high),&high,0) && (high.dwFlags&HCF_HIGHCONTRASTON)) return false;
    DWORD transparency=1,bytes=sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&bytes);
    return transparency!=0;
}
void Notify(DWORD target) {
    HWND window=nullptr;
    while((window=FindWindowExW(HWND_MESSAGE,window,StartNotifyClass,nullptr))) {
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid==target) PostMessageW(window,StartRefreshMessage,0,0);
    }
}
HRESULT Inject(DWORD pid,HANDLE process,HANDLE remoteMapping,ShellHost host) {
    // Earlier helpers stay pinned after an in-app update. Wait for their
    // restorations before v15 saves original shell properties; never unload them.
    struct LegacyState { volatile LONG magic,pid,epoch,running,enabled,light,attached,count,result,searches,spacing; };
    for (const auto version : {L"v1",L"v2",L"v3",L"v4",L"v5",L"v6",L"v7",L"v8",L"v9",L"v10",L"v11",L"v12",L"v13",L"v14"}) {
        Handle retired(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(L"Local\\jellyflower.Start."+std::wstring(version)+L"."+std::to_wstring(pid)).c_str()));
        if(!retired) continue;
        const std::wstring_view generation(version);
        const bool spacing=generation!=L"v1" && generation!=L"v2" && generation!=L"v3";
        const bool searches=generation!=L"v1" && generation!=L"v2";
        const size_t size=spacing?sizeof(LegacyState):searches?10*sizeof(LONG):9*sizeof(LONG);
        auto* previous=static_cast<LegacyState*>(MapViewOfFile(retired.get(),FILE_MAP_ALL_ACCESS,0,0,size));
        if(!previous) continue;
        bool ready=false;
        for(unsigned attempt=0;attempt<20;++attempt) {
            if(!Read(previous->count) && (!searches || !Read(previous->searches)) && (!spacing || !Read(previous->spacing))) {ready=true;break;}
            HWND receiver=nullptr;
            const auto notifyClass=L"jellyflower.StartNotify."+std::wstring(version);
            while((receiver=FindWindowExW(HWND_MESSAGE,receiver,notifyClass.c_str(),nullptr))) {
                DWORD owner=0;GetWindowThreadProcessId(receiver,&owner);
                if(owner==pid) PostMessageW(receiver,StartRefreshMessage,0,0);
            }
            Sleep(100);
        }
        UnmapViewOfFile(previous);
        if(!ready) return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    HRESULT result=E_FAIL;
    HMODULE xaml=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!xaml) return HRESULT_FROM_WIN32(GetLastError());
    struct Library {HMODULE value;~Library(){FreeLibrary(value);}} library{xaml};
    const auto initialize=reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(GetProcAddress(xaml,"InitializeXamlDiagnosticsEx"));
    wchar_t file[32768]{};const DWORD length=GetModuleFileNameW(nullptr,file,32768);
    if(initialize && length && length<32768) {
        std::wstring path(file);path.resize(path.find_last_of(L'\\')+1);path+=L"jellyflower-start-v15.dll";
        if(host==ShellHost::Search) {
            result=SearchHelperAccess(process,path.c_str());if(FAILED(result))return result;
        }
        wchar_t data[64]{};swprintf_s(data,L"JFStart15 %llx",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(remoteMapping)));
        // Initialization runs on a fresh background thread. Probe existing
        // endpoint names until one accepts; never create/replace Start.
        for(unsigned index=1;index<=64;++index) {
            const auto endpoint=L"VisualDiagConnection"+std::to_wstring(index);
            result=initialize(endpoint.c_str(),pid,nullptr,path.c_str(),StartTapClass,data);
            if(result!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
        }
    } else result=HRESULT_FROM_WIN32(initialize ? ERROR_BAD_PATHNAME:ERROR_PROC_NOT_FOUND);
    return result;
}
}
void StartSession::Release() {
    if(state_) {InterlockedExchange(&state_->running,0);Notify(pid_);}
    if(worker_.joinable()) worker_.join();
    if(state_) {
        CloseRemote(process_.get(),reinterpret_cast<HANDLE>(static_cast<uintptr_t>(InterlockedExchange64(&state_->ownerHandle,0))));
        CloseRemote(process_.get(),remoteMapping_);
    }
    remoteMapping_=nullptr;
    if(state_) {UnmapViewOfFile(state_);state_=nullptr;}
    mapping_.reset();process_.reset();pid_=0;lastAttempt_=0;
}
StartSession::~StartSession() {Release();}
void StartSession::Refresh(bool light) {
    if(worker_.joinable() && finished_) worker_.join();
    if(process_ && WaitForSingleObject(process_.get(),0)!=WAIT_TIMEOUT) Release();
    bool newOwner=false;
    if(!pid_) {
        pid_=StartPid(host_);if(!pid_) return;
        process_.reset(OpenProcess(SYNCHRONIZE|PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid_));
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(StartState),StartMappingName(pid_).c_str()));
        const bool existingMapping=GetLastError()==ERROR_ALREADY_EXISTS;
        if(process_ && mapping_) state_=static_cast<StartState*>(MapViewOfFile(mapping_.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(StartState)));
        if(!state_) {Release();return;}
        if(!existingMapping)wcsncpy_s(state_->placeholder,DeviceSearchPlaceholder().c_str(),_TRUNCATE);
        const auto lifetime=DuplicateTo(GetCurrentProcess(),process_.get(),SYNCHRONIZE);
        if(!lifetime){Release();return;}
        CloseRemote(process_.get(),reinterpret_cast<HANDLE>(static_cast<uintptr_t>(InterlockedExchange64(&state_->ownerHandle,reinterpret_cast<LONG64>(lifetime)))));
        InterlockedExchange(&state_->magic,StartMagic);
        InterlockedExchange(&state_->controllerPid,GetCurrentProcessId());
        InterlockedIncrement(&state_->epoch);InterlockedExchange(&state_->running,1);
        newOwner=true;
    }
    const LONG enabled=EffectsEnabled() ? 1:0;
    const bool changed=InterlockedExchange(&state_->enabled,enabled)!=enabled;
    const bool themeChanged=InterlockedExchange(&state_->light,light ? 1:0)!=(light ? 1:0);
    if(newOwner || changed || themeChanged) Notify(pid_);
    if(Read(state_->attached)==1 || worker_.joinable() || (lastAttempt_ && GetTickCount64()-lastAttempt_<30000)) return;
    if(!remoteMapping_) {
        InterlockedExchange(&state_->mappingClaimed,0);
        remoteMapping_=DuplicateTo(mapping_.get(),process_.get(),FILE_MAP_READ|FILE_MAP_WRITE);
        if(!remoteMapping_){InterlockedExchange(&state_->result,HRESULT_FROM_WIN32(GetLastError()));return;}
    }
    lastAttempt_=GetTickCount64();finished_=false;
    InterlockedExchange(&state_->result,S_OK);
    try {
        worker_=std::thread([this] {
            HRESULT result=E_FAIL;
            try {result=Inject(pid_,process_.get(),remoteMapping_,host_);} catch(...) {}
            if(SUCCEEDED(result)) {
                for(unsigned i=0;i<20 && Read(state_->running) && !Read(state_->attached);++i)Sleep(100);
                if(!Read(state_->attached))result=HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            if(FAILED(result)) InterlockedExchange(&state_->result,result);
            finished_=true;
        });
    } catch(...) {
        finished_=true;
        InterlockedExchange(&state_->result,HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));
    }
}
}

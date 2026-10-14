// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the production coroutine with fake one-shot location and registry APIs.
#include <windows.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <coroutine>
#include <chrono>
#include <memory>
#include <utility>
#include <map>
#include <vector>
#include <iostream>
#include <stdexcept>
namespace {
using winrt::Windows::Foundation::IAsyncInfo;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Devices::Geolocation::GeolocationAccessStatus;
struct Pending {AsyncStatus status=AsyncStatus::Completed;HRESULT error=S_OK;std::coroutine_handle<> continuation;};
int locators=0,infos=0,queries=0,consents=0,cancels=0;
bool holdPosition=false,holdConsent=false,deny=false,fail=false;
ULONGLONG testTick=1000;
std::weak_ptr<Pending> outstanding;
std::map<std::wstring,std::vector<BYTE>> values;
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
struct Info:winrt::implements<Info,IAsyncInfo>{
    std::shared_ptr<Pending> operation;
    explicit Info(std::shared_ptr<Pending> p):operation(std::move(p)){++infos;}
    ~Info(){--infos;}
    uint32_t Id()const{return 1;}
    AsyncStatus Status()const{return operation->status;}
    winrt::hresult ErrorCode()const{return operation->error;}
    void Close()const{}
    void Cancel()const{if(operation->status!=AsyncStatus::Started)return;++cancels;operation->status=AsyncStatus::Canceled;operation->error=HRESULT_FROM_WIN32(ERROR_CANCELLED);const auto resume=std::exchange(operation->continuation,{});if(resume)resume.resume();}
};
template<class T>struct Operation{
    std::shared_ptr<Pending> operation;T result;
    template<class U>U as()const{return winrt::make<Info>(operation).template as<U>();}
    struct Awaiter{
        std::shared_ptr<Pending> operation;T result;
        bool await_ready()const{return operation->status!=AsyncStatus::Started;}
        void await_suspend(std::coroutine_handle<> continuation)const{operation->continuation=continuation;}
        T await_resume()const{winrt::check_hresult(operation->error);return result;}
    };
    Awaiter operator co_await()const{return {operation,result};}
};
struct Point{winrt::Windows::Devices::Geolocation::BasicGeoposition Position()const{return {34.123,-118.245,0};}};
struct Coordinate{::Point Point()const{return {};}};
struct Position{::Coordinate Coordinate()const{return {};}};
struct ProbeGeolocator{
    ProbeGeolocator(){++locators;}
    ~ProbeGeolocator(){--locators;}
    ProbeGeolocator(const ProbeGeolocator&)=delete;
    void DesiredAccuracyInMeters(uint32_t accuracy)const{Check(accuracy==10000,"city-level accuracy retained");}
    static Operation<GeolocationAccessStatus> RequestAccessAsync(){
        ++consents;auto pending=std::make_shared<Pending>();if(holdConsent)pending->status=AsyncStatus::Started;outstanding=pending;
        return {pending,deny?GeolocationAccessStatus::Denied:GeolocationAccessStatus::Allowed};
    }
    Operation<::Position> GetGeopositionAsync(std::chrono::hours age,std::chrono::seconds timeout)const{
        ++queries;Check(age==std::chrono::hours(1) && timeout==std::chrono::seconds(15),"request accepts cached fixes and has a 15-second bound");
        if(deny)throw winrt::hresult_access_denied();if(fail)throw winrt::hresult_error(E_FAIL);
        auto pending=std::make_shared<Pending>();if(holdPosition)pending->status=AsyncStatus::Started;outstanding=pending;return {pending,{}};
    }
};
LSTATUS WINAPI Create(HKEY,LPCWSTR,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY key,LPDWORD){*key=reinterpret_cast<HKEY>(1);return 0;}
LSTATUS WINAPI Set(HKEY,LPCWSTR name,DWORD,DWORD,const BYTE* data,DWORD size){values[name]={data,data+size};return 0;}
LSTATUS WINAPI Read(HKEY,LPCWSTR,LPCWSTR name,DWORD,LPDWORD,void* data,LPDWORD size){const auto found=values.find(name);if(found==values.end())return ERROR_FILE_NOT_FOUND;if(*size<found->second.size())return ERROR_MORE_DATA;*size=static_cast<DWORD>(found->second.size());memcpy(data,found->second.data(),*size);return 0;}
LSTATUS WINAPI Delete(HKEY,LPCWSTR name){values.erase(name);return 0;}
LSTATUS WINAPI Close(HKEY){return 0;}
ULONGLONG WINAPI Clock(){return testTick;}
void Reset(){Check(!locators && !infos,"previous request released all location objects");values.clear();queries=consents=cancels=0;holdPosition=holdConsent=deny=fail=false;testTick=1000;outstanding.reset();}
DWORD Number(const wchar_t* name){DWORD value=0;const auto& bytes=values.at(name);Check(bytes.size()==sizeof(value),"setting is DWORD");memcpy(&value,bytes.data(),sizeof(value));return value;}
}
#define RegCreateKeyExW Create
#define RegSetValueExW Set
#define RegGetValueW Read
#define RegDeleteValueW Delete
#define RegCloseKey Close
#define GetTickCount64 Clock
#define Geolocator ProbeGeolocator
#include "../src/auto_theme.cpp"
using namespace acrylic;
int main(){try{
    for(const auto scope:{AutoThemeScope::System,AutoThemeScope::Wallpaper}){
        Reset();AutoTheme theme(nullptr,scope);theme.Enable();
        Check(theme.Status()==AutoThemeStatus::Automatic && queries==1 && consents==1 && !locators && !infos,"successful acquisition retains neither locator nor completed operation");
        Coordinates rounded{};memcpy(&rounded,values.at(AutoLocationValue(scope)).data(),sizeof(rounded));Check(rounded.latitude==34.1 && rounded.longitude==-118.2,"only rounded coordinates are retained");
        bool light=false;UINT delay=0;for(int i=0;i<100;++i)Check(theme.Update(light,delay) && delay && !locators && !infos,"cached solar decisions need no live location object");
        Check(queries==1,"ordinary timer/system events do not repeatedly acquire location");
        testTick+=24ull*60*60*1000;theme.Update(light,delay);Check(queries==2 && consents==1 && !locators && !infos,"daily refresh is one-shot without another consent prompt");
        Check(!theme.Disable() && Number(AutoThemeValue(scope))==0,"disable clears automatic preference");testTick+=48ull*60*60*1000;
        Check(!theme.Update(light,delay) && !delay && queries==2 && !locators && !infos,"manual mode never schedules location work");
    }
    Reset();{
        holdPosition=true;AutoTheme theme(nullptr);theme.Enable();Check(locators==1 && infos==1 && queries==1,"pending acquisition owns one temporary locator and cancellation handle");
        Check(!theme.Disable() && cancels==1 && !locators && !infos && Number(L"AutoTheme")==0,"disable cancels pending acquisition outside lock and releases all resources");
        Check(!values.count(L"ThemeLocation"),"cancelled result cannot restore location after disable");
    }
    Reset();{holdPosition=true;auto theme=std::make_unique<AutoTheme>(nullptr);theme->Enable();Check(locators==1,"destruction fixture is waiting for a location");theme.reset();Check(cancels==1 && !locators && !infos && !values.count(L"ThemeLocation"),"owner destruction cancels acquisition and ignores stale completion");}
    Reset();{holdConsent=true;AutoTheme theme(nullptr);theme.Enable();Check(!locators && infos==1 && !queries,"no locator is created before consent resolves");theme.Disable();Check(cancels==1 && !queries && !locators && !infos,"cancelled consent cannot start a later GPS read");}
    Reset();{deny=true;AutoTheme theme(nullptr);theme.Enable();Check(theme.Status()==AutoThemeStatus::Denied && !queries && !locators && !infos,"denied consent leaves no live location object");}
    Reset();{AutoTheme theme(nullptr);theme.Enable();bool light=false;UINT delay=0;testTick+=24ull*60*60*1000;fail=true;theme.Update(light,delay);
        Check(theme.Status()==AutoThemeStatus::Automatic && !locators && !infos,"transient failure releases objects and retains cached solar decisions");
        fail=false;testTick+=15*60*1000;theme.Update(light,delay);Check(queries==3 && !locators && !infos,"transient retry is bounded and releases its objects");
        deny=true;testTick+=24ull*60*60*1000;theme.Update(light,delay);Check(theme.Status()==AutoThemeStatus::Denied && !locators && !infos && Number(L"AutoTheme")==0 && !values.count(L"ThemeLocation"),"permission denial during refresh clears the automatic cache without a watcher");
    }
    Reset();{AutoTheme theme(nullptr);theme.Enable();}queries=consents=0;{
        AutoTheme restored(nullptr);Check(restored.Status()==AutoThemeStatus::Automatic && queries==1 && !consents && !locators && !infos,"startup revalidation is one-shot and never prompts");
    }
    Check(!locators && !infos,"test ends with no outstanding location resources");
    std::cout<<"PASS: production one-shot location lifetime, cancellation, cached daily scheduling, both scopes, permission/failure/startup paths; no real location requests or settings changes.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
// Production preference paths with simulated registry/appbar APIs only.
#include <windows.h>
#include <shellapi.h>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
namespace {
std::map<std::wstring,std::vector<BYTE>> preferences;
std::vector<std::wstring> reads;
DWORD writeError=0,edge=ABE_BOTTOM,appbarFlags=0;unsigned setters=0;
bool available=true,taskbarPresent=true,ignoreSetter=false;
LSTATUS WINAPI TestCreate(HKEY,LPCWSTR,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY key,LPDWORD){*key=reinterpret_cast<HKEY>(1);return writeError;}
LSTATUS WINAPI TestSet(HKEY,LPCWSTR name,DWORD,DWORD,const BYTE* data,DWORD size){if(writeError)return writeError;preferences[name]={data,data+size};return ERROR_SUCCESS;}
LSTATUS WINAPI TestSetKey(HKEY,LPCWSTR,LPCWSTR name,DWORD type,LPCVOID data,DWORD size){return TestSet(nullptr,name,0,type,static_cast<const BYTE*>(data),size);}
LSTATUS WINAPI TestRead(HKEY,LPCWSTR,LPCWSTR name,DWORD,LPDWORD,void* data,LPDWORD size){reads.emplace_back(name);const auto found=preferences.find(name);if(found==preferences.end())return ERROR_FILE_NOT_FOUND;if(*size<found->second.size())return ERROR_MORE_DATA;*size=static_cast<DWORD>(found->second.size());memcpy(data,found->second.data(),*size);return ERROR_SUCCESS;}
LSTATUS WINAPI TestDelete(HKEY,LPCWSTR name){preferences.erase(name);return ERROR_SUCCESS;}
LSTATUS WINAPI TestClose(HKEY){return ERROR_SUCCESS;}
UINT_PTR WINAPI TestAppbar(DWORD message,PAPPBARDATA data){
    if(message==ABM_GETTASKBARPOS){data->uEdge=edge;return available;}
    if(message==ABM_GETSTATE)return appbarFlags;
    if(message==ABM_SETSTATE){++setters;if(!ignoreSetter)appbarFlags=static_cast<DWORD>(data->lParam);return TRUE;}
    return 0;
}
HWND WINAPI TestFind(LPCWSTR,LPCWSTR){return taskbarPresent?reinterpret_cast<HWND>(1):nullptr;}
BOOL WINAPI TestNotify(HWND,UINT,WPARAM,LPARAM){return TRUE;}
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
DWORD Number(const wchar_t* name){const auto& bytes=preferences.at(name);Check(bytes.size()==sizeof(DWORD),"DWORD setting size");DWORD value;memcpy(&value,bytes.data(),sizeof(value));return value;}
}
#define RegCreateKeyExW TestCreate
#define RegSetValueExW TestSet
#define RegSetKeyValueW TestSetKey
#define RegGetValueW TestRead
#define RegDeleteValueW TestDelete
#define RegCloseKey TestClose
#include "../src/auto_theme.cpp"
#include "../src/wallpaper_theme.hpp"
#define SHAppBarMessage TestAppbar
#define FindWindowW TestFind
#define SendNotifyMessageW TestNotify
#include "../src/taskbar_options.hpp"
using namespace acrylic;
int main(){try{
    Check(LoadWallpaperTheme()==WallpaperTheme::System,"wallpaper defaults to system on missing preference");
    Check(!SaveWallpaperTheme(WallpaperTheme::Auto) && LoadWallpaperTheme()==WallpaperTheme::Auto,"wallpaper auto persists separately");
    writeError=ERROR_ACCESS_DENIED;Check(SaveWallpaperTheme(WallpaperTheme::System)==writeError && LoadWallpaperTheme()==WallpaperTheme::Auto,"failed setting write preserves selected mode");writeError=0;
    Check(DecodeWallpaperTheme(99)==WallpaperTheme::System,"unknown persisted modes fall back to system");
    for(bool system:{false,true})for(bool ready:{false,true})for(bool solar:{false,true}){
        Check(WallpaperUsesLight(WallpaperTheme::System,system,ready,solar)==system,"system wallpaper ignores solar schedule");
        Check(WallpaperUsesLight(WallpaperTheme::Auto,system,ready,solar)==(ready?solar:system),"auto is independent with safe system fallback before location is ready");
    }
    {AutoTheme system(nullptr),wallpaper(nullptr,AutoThemeScope::Wallpaper);UINT delay=9;bool light=false;
        Check(system.Status()==AutoThemeStatus::Manual && wallpaper.Status()==AutoThemeStatus::Manual,"independent automatic schedulers start inactive without approved locations");
        Check(reads.back()==L"WallpaperAutoTheme","wallpaper startup reads only its own automatic flag");
        const Coordinates point{34.1,-118.2};Check(!Save(true,&point,AutoThemeScope::System) && !Save(true,&point,AutoThemeScope::Wallpaper),"both approved-location records saved through production path");
        Check(!wallpaper.Disable() && Number(L"AutoTheme")==1 && preferences.count(L"ThemeLocation") && !preferences.count(L"WallpaperThemeLocation"),"wallpaper disable never removes system schedule/location");
        Check(!Save(true,&point,AutoThemeScope::Wallpaper) && !system.Disable() && Number(L"WallpaperAutoTheme")==1 && preferences.count(L"WallpaperThemeLocation") && !preferences.count(L"ThemeLocation"),"system disable never removes wallpaper schedule/location");
        Check(!wallpaper.Update(light,delay) && !delay,"inactive scheduler has no timer or location request");
    }
    const DWORD record[]{48,0xfffffffeu,2,ABE_BOTTOM,288,72,0,1528,2400,1600,144,1};
    preferences[L"Settings"]={reinterpret_cast<const BYTE*>(record),reinterpret_cast<const BYTE*>(record)+sizeof(record)};
    Check(ReadTaskbarOptions().Packed()==2,"bottom taskbar off is enabled");
    appbarFlags=ABS_ALWAYSONTOP|0x80;Check(ToggleTaskbarAutoHide().autoHide && appbarFlags==(ABS_ALWAYSONTOP|0x80|ABS_AUTOHIDE),"native auto hide preserves all other appbar appbarFlags");
    Check(!ToggleTaskbarAutoHide().autoHide && appbarFlags==(ABS_ALWAYSONTOP|0x80),"native auto hide turns off again");
    Check(preferences[L"Settings"].size()==sizeof(record) && memcmp(preferences[L"Settings"].data(),record,sizeof(record))==0,"round trip preserves every unrelated saved taskbar byte");
    writeError=ERROR_ACCESS_DENIED;Check(ToggleTaskbarAutoHide().error==ERROR_ACCESS_DENIED && !(appbarFlags&ABS_AUTOHIDE),"failed persistence rolls back live auto-hide only");writeError=0;
    preferences[L"Settings"][4]=0;const auto unsupportedSetters=setters;Check(ToggleTaskbarAutoHide().error==ERROR_NOT_SUPPORTED && setters==unsupportedSetters,"unknown shell record never changes live or saved settings");preferences[L"Settings"][4]=0xfe;
    for(DWORD other:std::vector<DWORD>{ABE_LEFT,ABE_TOP,ABE_RIGHT,99u}){edge=other;const auto before=setters;Check(!ToggleTaskbarAutoHide().bottom && setters==before,"non-bottom or unknown edges never call the native setter");}
    edge=ABE_BOTTOM;const auto old=ReadTaskbarOptions();edge=ABE_TOP;const auto before=setters;Check(old.bottom && !ToggleTaskbarAutoHide().bottom && setters==before,"edge is rechecked on click after an earlier enabled menu snapshot");
    edge=ABE_BOTTOM;available=false;Check(ToggleTaskbarAutoHide().error==ERROR_NOT_READY && setters==before,"failed taskbar query cannot change settings");available=true;
    taskbarPresent=false;Check(ToggleTaskbarAutoHide().error==ERROR_NOT_READY && setters==before,"missing Explorer window cannot change settings");taskbarPresent=true;
    ignoreSetter=true;Check(ToggleTaskbarAutoHide().error==ERROR_WRITE_FAULT,"ABM_SETSTATE's unconditional TRUE is checked against actual resulting state");
    std::cout<<"PASS: independent wallpaper preference/solar policy, scoped auto/location persistence, sleeping scheduler, native auto-hide guards/appbarFlags/state verification; no real settings or location requests.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
// Production tray path with simulated shell/icon/theme APIs; no desktop writes.
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace {
bool exists=false;DWORD light=0;UINT dpi=96;unsigned loads=0,destroys=0,branding=0;
std::vector<DWORD> messages,flags;
BOOL WINAPI Notify(DWORD message,PNOTIFYICONDATAW data){
    messages.push_back(message);flags.push_back(data->uFlags);
    if(message==NIM_ADD){exists=true;return TRUE;}
    if(message==NIM_DELETE){exists=false;return TRUE;}
    if(message==NIM_SETVERSION)return exists && data->uVersion==NOTIFYICON_VERSION_4;
    return exists;
}
HANDLE WINAPI Load(HINSTANCE,LPCWSTR,UINT,int,int,UINT){return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(++loads));}
BOOL WINAPI Destroy(HICON){++destroys;return TRUE;}
LSTATUS WINAPI ReadSetting(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD,void* data,LPDWORD size){if(*size<sizeof(light))return ERROR_MORE_DATA;memcpy(data,&light,sizeof(light));*size=sizeof(light);return ERROR_SUCCESS;}
BOOL WINAPI SystemInfo(UINT,UINT,void* data,UINT){static_cast<HIGHCONTRASTW*>(data)->dwFlags=0;return TRUE;}
UINT WINAPI Dpi(HWND){return dpi;}
int WINAPI Metric(int,UINT value){return MulDiv(16,value,96);}
HWND WINAPI Find(LPCWSTR,LPCWSTR){return reinterpret_cast<HWND>(1);}
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
#define Shell_NotifyIconW Notify
#define LoadImageW Load
#define DestroyIcon Destroy
#define RegGetValueW ReadSetting
#define SystemParametersInfoW SystemInfo
#define GetDpiForWindow Dpi
#define GetSystemMetricsForDpi Metric
#define FindWindowW Find
#include "../src/tray.cpp"
namespace acrylic {
void RefreshAppBranding(HWND,bool){++branding;}
UINT ShowTrayMenu(HWND,POINT,UINT,AcrylicMode,bool,UINT,const RECT&){return 0;}
}
int main(){acrylic::SetTextLanguage(acrylic::Language::English);try{
    const auto owner=reinterpret_cast<HWND>(1);
    {acrylic::TrayIcon icon;icon.Refresh(owner,acrylic::AcrylicMode::System);Check(icon.Present() && exists && loads==1,"initial add uses cached themed artwork");
     const auto count=messages.size();for(int i=0;i<100;++i)icon.Refresh(owner,i%2?acrylic::AcrylicMode::Pure:acrylic::AcrylicMode::System);
     Check(messages.size()==count && loads==1 && branding==1,"routine/mode refreshes never resend unchanged icon");
     light=1;icon.Refresh(owner,acrylic::AcrylicMode::Pure);Check(messages.size()==count+1 && messages.back()==NIM_MODIFY && loads==2,"theme change updates artwork once");
     const auto themed=messages.size();dpi=144;icon.Refresh(owner,acrylic::AcrylicMode::Pure);Check(messages.size()==themed+1 && loads==3,"DPI change updates artwork once");
     exists=false;icon.ShellRestarted();icon.Refresh(owner,acrylic::AcrylicMode::Pure);Check(icon.Present() && exists && messages.back()==NIM_SETVERSION && loads==3,"Explorer restart adds existing cached icon and restores v4 callbacks");
     const auto languageCount=messages.size();acrylic::SetTextLanguage(acrylic::Language::French);icon.Refresh(owner,acrylic::AcrylicMode::Pure);
     Check(messages.size()==languageCount+1 && messages.back()==NIM_MODIFY && loads==3 && (flags.back()&NIF_TIP) && !(flags.back()&NIF_ICON),"language change refreshes tooltip without loading artwork");
    }Check(!exists && loads==destroys,"exit removes tray icon and releases owned handles");
    std::puts("PASS: unchanged tray refresh suppression, theme/DPI updates, Explorer restart recovery, cleanup; no real shell writes.");return 0;
}catch(std::exception const& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}

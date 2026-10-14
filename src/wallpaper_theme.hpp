// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
namespace acrylic {
enum class WallpaperTheme { System, Auto };
inline WallpaperTheme DecodeWallpaperTheme(DWORD value){return value==1?WallpaperTheme::Auto:WallpaperTheme::System;}
inline WallpaperTheme LoadWallpaperTheme(){
    DWORD value=0,size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\jellyflower",L"WallpaperTheme",RRF_RT_REG_DWORD,nullptr,&value,&size))return WallpaperTheme::System;
    return DecodeWallpaperTheme(value);
}
inline DWORD SaveWallpaperTheme(WallpaperTheme theme){
    const DWORD value=static_cast<DWORD>(theme);
    return RegSetKeyValueW(HKEY_CURRENT_USER,L"Software\\jellyflower",L"WallpaperTheme",REG_DWORD,&value,sizeof(value));
}
inline bool WallpaperUsesLight(WallpaperTheme theme,bool systemLight,bool solarReady,bool solarLight){
    return theme==WallpaperTheme::Auto && solarReady?solarLight:systemLight;
}
}

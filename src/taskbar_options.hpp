// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <cstring>

namespace acrylic {
struct TaskbarOptions {
    bool autoHide=false, bottom=false;
    DWORD error=0;
    UINT Packed() const {return (autoHide?1u:0u)|(bottom && !error?2u:0u);}
};
// This is the native Windows preference, not an app-owned override. Preserve
// all other state flags and recheck the edge before every explicit change.
inline TaskbarOptions ReadTaskbarOptions(){
    APPBARDATA data{sizeof(data)};
    if(!SHAppBarMessage(ABM_GETTASKBARPOS,&data))return {false,false,ERROR_NOT_READY};
    const auto flags=SHAppBarMessage(ABM_GETSTATE,&data);
    return {(flags&ABS_AUTOHIDE)!=0,data.uEdge==ABE_BOTTOM,0};
}
// Windows 11's appbar API can update only Explorer's live state. Persist the
// single auto-hide bit in the existing shell record as well. This record is
// private: accept only the known, size/signature/edge-checked layout, preserve
// every other byte, and never manufacture a record on an unfamiliar shell.
struct TaskbarRecord {
    std::array<DWORD,12> data{};
    DWORD size=sizeof(data);
};
inline DWORD ReadTaskbarRecord(TaskbarRecord& record){
    const auto error=RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3",L"Settings",RRF_RT_REG_BINARY,nullptr,record.data.data(),&record.size);
    if(error)return error;
    if((record.size!=40 && record.size!=48) || record.data[0]!=record.size || record.data[1]!=0xfffffffeu || record.data[3]!=ABE_BOTTOM || (record.data[2]&~3u))return ERROR_NOT_SUPPORTED;
    return ERROR_SUCCESS;
}
inline DWORD PersistTaskbarAutoHide(bool enabled){
    TaskbarRecord record;const auto error=ReadTaskbarRecord(record);if(error)return error;
    const DWORD desired=(record.data[2]&~ABS_AUTOHIDE)|(enabled?ABS_AUTOHIDE:0u);
    if(desired==record.data[2])return ERROR_SUCCESS;
    record.data[2]=desired;
    return RegSetKeyValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3",L"Settings",REG_BINARY,record.data.data(),record.size);
}
inline TaskbarOptions ToggleTaskbarAutoHide(){
    const auto before=ReadTaskbarOptions();
    if(before.error || !before.bottom)return before;
    APPBARDATA data{sizeof(data)};data.hWnd=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(!data.hWnd)return {before.autoHide,false,ERROR_NOT_READY};
    TaskbarRecord original;const auto stored=ReadTaskbarRecord(original);
    if(stored)return {before.autoHide,before.bottom,stored};
    data.lParam=static_cast<LPARAM>(SHAppBarMessage(ABM_GETSTATE,&data)^ABS_AUTOHIDE);
    SHAppBarMessage(ABM_SETSTATE,&data);
    auto after=ReadTaskbarOptions();
    if(!after.error && after.autoHide==before.autoHide)after.error=ERROR_WRITE_FAULT;
    if(!after.error)after.error=PersistTaskbarAutoHide(after.autoHide);
    if(after.error){
        // No half-applied preference on a denied registry write. Re-read live
        // flags so rollback also preserves unrelated changes made by Windows.
        data.lParam=static_cast<LPARAM>((SHAppBarMessage(ABM_GETSTATE,&data)&~ABS_AUTOHIDE)|(before.autoHide?ABS_AUTOHIDE:0u));
        SHAppBarMessage(ABM_SETSTATE,&data);
        return {ReadTaskbarOptions().autoHide,before.bottom,after.error};
    }
    // Let Settings and secondary taskbars refresh from the same preference.
    SendNotifyMessageW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"TraySettings"));
    return after;
}
}

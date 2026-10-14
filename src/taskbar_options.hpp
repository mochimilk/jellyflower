// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <shellapi.h>

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
inline TaskbarOptions ToggleTaskbarAutoHide(){
    const auto before=ReadTaskbarOptions();
    if(before.error || !before.bottom)return before;
    APPBARDATA data{sizeof(data)};data.hWnd=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(!data.hWnd)return {before.autoHide,false,ERROR_NOT_READY};
    data.lParam=static_cast<LPARAM>(SHAppBarMessage(ABM_GETSTATE,&data)^ABS_AUTOHIDE);
    SHAppBarMessage(ABM_SETSTATE,&data);
    auto after=ReadTaskbarOptions();
    if(!after.error && after.autoHide==before.autoHide)after.error=ERROR_WRITE_FAULT;
    return after;
}
}

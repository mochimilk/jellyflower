// SPDX-License-Identifier: GPL-3.0-or-later
#include "search_text.hpp"
#include <lm.h>
#include <cwctype>
namespace acrylic {
std::wstring SearchFirstName(std::wstring_view displayName) {
    const auto space=[](wchar_t c){return std::iswspace(c) || c==0x3000 || c==0xa0;};
    // Windows has a display name, not a universal given-name field. Handle
    // common "given family" and "family, given" formats without recasing it.
    if(const auto comma=displayName.find(L',');comma!=std::wstring_view::npos)displayName.remove_prefix(comma+1);
    while(!displayName.empty() && space(displayName.front()))displayName.remove_prefix(1);
    size_t length=0;while(length<displayName.size() && length<64 && !space(displayName[length]))++length;
    if(length && displayName[length-1]>=0xd800 && displayName[length-1]<=0xdbff)--length;
    return std::wstring(displayName.substr(0,length));
}
const std::wstring& UserSearchFirstName() {
    // Only the desktop controller reads the current local account, once.
    // No remote server, location, permission prompt or account enumeration.
    static const std::wstring first=[] {
        wchar_t account[256]{};DWORD size=256;
        if(!GetUserNameW(account,&size))return std::wstring{};
        LPBYTE buffer=nullptr;std::wstring display;
        if(NetUserGetInfo(nullptr,account,10,&buffer)==NERR_Success && buffer){
            const auto info=reinterpret_cast<const USER_INFO_10*>(buffer);
            if(info->usri10_full_name)display=info->usri10_full_name;
        }
        if(buffer)NetApiBufferFree(buffer);
        auto name=SearchFirstName(display);return name.empty()?SearchFirstName(account):name;
    }();
    return first;
}
}

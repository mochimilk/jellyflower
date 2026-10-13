// SPDX-License-Identifier: GPL-3.0-or-later
#include "search_text.hpp"
#include <lm.h>
#include <cwctype>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
namespace acrylic {
std::wstring SearchGivenName(std::wstring_view givenName) {
    const auto space=[](wchar_t c){return std::iswspace(c) || c==0x3000 || c==0xa0;};
    while(!givenName.empty() && space(givenName.front()))givenName.remove_prefix(1);
    while(!givenName.empty() && space(givenName.back()))givenName.remove_suffix(1);
    size_t length=std::min<size_t>(64,givenName.size());
    if(length && givenName[length-1]>=0xd800 && givenName[length-1]<=0xdbff)--length;
    return std::wstring(givenName.substr(0,length));
}
std::wstring SearchFirstName(std::wstring_view displayName) {
    const auto space=[](wchar_t c){return std::iswspace(c) || c==0x3000 || c==0xa0;};
    // The local-account fallback exposes a full display name. Handle common
    // "given family" and "family, given" formats without recasing it.
    if(const auto comma=displayName.find(L',');comma!=std::wstring_view::npos)displayName.remove_prefix(comma+1);
    while(!displayName.empty() && space(displayName.front()))displayName.remove_prefix(1);
    size_t length=0;while(length<displayName.size() && length<64 && !space(displayName[length]))++length;
    if(length && displayName[length-1]>=0xd800 && displayName[length-1]<=0xdbff)--length;
    return std::wstring(displayName.substr(0,length));
}
namespace {
std::wstring ReadFirstName() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment{~Apartment(){winrt::uninit_apartment();}} apartment;
        const auto user=winrt::Windows::System::User::GetDefault();
        if(user){
            auto query=user.GetPropertyAsync(winrt::Windows::System::KnownUserProperties::FirstName());
            if(query.wait_for(std::chrono::seconds(2))==winrt::Windows::Foundation::AsyncStatus::Completed){
                const auto given=SearchGivenName(winrt::unbox_value_or<winrt::hstring>(query.GetResults(),L""));
                if(!given.empty())return given;
            }else query.Cancel();
        }
    }catch(...){/* Older Windows/privacy policies may not expose this property. */}
    try {
        wchar_t account[256]{};DWORD size=256;
        if(!GetUserNameW(account,&size))return {};
        struct Buffer{LPBYTE data=nullptr;~Buffer(){if(data)NetApiBufferFree(data);}} buffer;
        if(NetUserGetInfo(nullptr,account,10,&buffer.data)==NERR_Success && buffer.data){
            const auto info=reinterpret_cast<const USER_INFO_10*>(buffer.data);
            if(info->usri10_full_name)return SearchFirstName(info->usri10_full_name);
        }
    }catch(...){}
    return {}; // A sign-in identifier is not a first name.
}
struct NameCache {
    std::wstring first;
    std::atomic_bool ready{false};
    std::thread worker;
    explicit NameCache(HWND notify):worker([this,notify]{
        first=ReadFirstName();ready.store(true,std::memory_order_release);
        if(notify)PostMessageW(notify,SearchIdentityReadyMessage,0,0);
    }){}
    ~NameCache(){if(worker.joinable())worker.join();}
};
}
const std::wstring* UserSearchFirstName(HWND notify) {
    // Exactly one bounded background request in the desktop controller. The
    // immutable result is shared with Start/Search; their UI threads never wait.
    static NameCache cache(notify);
    return cache.ready.load(std::memory_order_acquire)?&cache.first:nullptr;
}
}

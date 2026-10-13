// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include <string_view>
namespace acrylic {
inline constexpr size_t SearchPlaceholderCapacity=264;
inline std::wstring SearchPlaceholder(std::wstring_view computerName) {
    std::wstring text=L"Search";
    if(!computerName.empty()){text+=L' ';text+=computerName;}
    return text;
}
inline const std::wstring& DeviceSearchPlaceholder() {
    // Read once in the desktop controller. SearchHost receives the immutable
    // result through its existing state mapping, without a sandbox API query.
    static const std::wstring text=[] {
        wchar_t name[256]{};DWORD length=256;
        if(!GetComputerNameExW(ComputerNamePhysicalDnsHostname,name,&length)) {
            length=256;
            if(!GetComputerNameW(name,&length))return SearchPlaceholder({});
        }
        return SearchPlaceholder(std::wstring_view(name,length));
    }();
    return text;
}
}

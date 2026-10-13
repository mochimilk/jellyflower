// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include <string_view>
namespace acrylic {
inline constexpr size_t SearchPlaceholderCapacity=264;
inline unsigned SearchGreetingPeriod(unsigned hour) noexcept {return hour>=5 && hour<12?0:hour>=12 && hour<18?1:2;}
inline unsigned LocalSearchGreetingPeriod() noexcept {SYSTEMTIME now{};GetLocalTime(&now);return SearchGreetingPeriod(now.wHour);}
inline std::wstring SearchGreeting(std::wstring_view firstName,unsigned period) {
    constexpr std::wstring_view words[]={L"Good morning",L"Good afternoon",L"Good evening"};
    constexpr std::wstring_view icons[]={L"\U0001f90d",L"\U0001f9f8",L"\U0001f607"};
    if(period>2)period=2;
    std::wstring text(words[period]);
    if(!firstName.empty()){text+=L", ";text+=firstName;}
    text+=L' ';text+=icons[period];return text;
}
std::wstring SearchFirstName(std::wstring_view displayName);
const std::wstring& UserSearchFirstName();
}

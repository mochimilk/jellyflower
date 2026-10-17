// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "localization.hpp"
#include <windows.h>
#include <string>
#include <string_view>
namespace acrylic {
inline constexpr size_t SearchPlaceholderCapacity=264;
inline unsigned SearchGreetingPeriod(unsigned hour) noexcept {return hour>=5 && hour<12?0:hour>=12 && hour<18?1:2;}
inline unsigned LocalSearchGreetingPeriod() noexcept {SYSTEMTIME now{};GetLocalTime(&now);return SearchGreetingPeriod(now.wHour);}
inline std::wstring SearchGreeting(std::wstring_view firstName,unsigned period,Language language=GetLanguage()) {
    constexpr TextId words[]={TextId::Morning,TextId::Afternoon,TextId::Evening};
    constexpr std::wstring_view icons[]={L"\U0001f90d",L"\U0001f9f8",L"\U0001f607"};
    if(period>2)period=2;
    return Text(firstName.empty()?TextId::GreetingAnonymous:TextId::GreetingNamed,
        {{L"greeting",Tr(words[period],language)},{L"name",firstName},{L"emoji",icons[period]}},language);
}
std::wstring SearchFirstName(std::wstring_view displayName);
std::wstring SearchGivenName(std::wstring_view givenName);
inline constexpr UINT SearchIdentityReadyMessage=WM_APP+84;
// Null while the one-time background lookup is pending; empty when unavailable.
const std::wstring* UserSearchFirstName(HWND notify=nullptr);
}

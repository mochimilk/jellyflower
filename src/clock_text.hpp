// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include "localization.hpp"
namespace acrylic {
// UI language and regional formats are different Windows preferences. Let NLS
// supply the regional weekday/month names, date order and 12/24-hour clock.
inline std::wstring LocalizedClock(const SYSTEMTIME& time,bool date=true,const wchar_t* locale=LOCALE_NAME_USER_DEFAULT) {
    if(time.wHour>23 || time.wMinute>59 || (date && (time.wDayOfWeek>6 || time.wMonth<1 || time.wMonth>12)))return {};
    wchar_t dateText[128]{},timeText[128]{},lower[128]{},order[4]{},name[LOCALE_NAME_MAX_LENGTH]{};
    if(!GetTimeFormatEx(locale,TIME_NOSECONDS,&time,nullptr,timeText,128))return {};
    if(LCMapStringEx(locale,LCMAP_LOWERCASE,timeText,-1,lower,128,nullptr,nullptr,0))wcscpy_s(timeText,lower);
    if(!date)return timeText;
    const wchar_t* pattern=L"ddd MMM d";
    if(GetLocaleInfoEx(locale,LOCALE_IDATE,order,4) && order[0]==L'1')pattern=L"ddd d MMM";
    if(locale)wcsncpy_s(name,locale,_TRUNCATE);else GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH);
    Language language{};
    if(MatchLanguage(name,language) && language==Language::Korean)pattern=L"M'월' d'일' ddd";
    if(!GetDateFormatEx(locale,0,&time,pattern,dateText,128,nullptr))return {};
    return std::wstring(dateText)+L"  "+timeText;
}
} // namespace acrylic

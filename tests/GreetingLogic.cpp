// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/search_text.hpp"
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
int main(){try{
    for(unsigned hour=0;hour<24;++hour){const unsigned expected=hour<5 || hour>=18?2:hour<12?0:1;Check(SearchGreetingPeriod(hour)==expected,"every local-clock hour selects the intended greeting");}
    Check(SearchGreeting(L"Jelly",0)==L"Good morning, Jelly \U0001f90d","morning white heart");
    Check(SearchGreeting(L"Jelly",1)==L"Good afternoon, Jelly \U0001f9f8","afternoon teddy bear");
    Check(SearchGreeting(L"Jelly",2)==L"Good evening, Jelly \U0001f607","evening angel");
    Check(SearchGreeting({},0)==L"Good morning \U0001f90d","unavailable name omits the comma");
    Check(SearchFirstName(L"  Eunsoo Lee ")==L"Eunsoo" && SearchFirstName(L"Lee, Eunsoo")==L"Eunsoo","common display-name orders yield given name");
    Check(SearchFirstName(L"Anne-Marie O'Neil")==L"Anne-Marie" && SearchFirstName(L"名 姓")==L"名" && SearchFirstName(L"\u3000名\u3000姓")==L"名","name punctuation, Unicode and whitespace retained");
    Check(SearchFirstName({}).empty() && SearchFirstName(L" \t ").empty(),"blank display-name fallback");
    const auto bounded=SearchFirstName(std::wstring(400,L'x'));Check(bounded.size()==64 && SearchGreeting(bounded,1).size()<SearchPlaceholderCapacity,"account text stays within transport capacity");
    auto edge=std::wstring(63,L'x')+L"\U0001f607";Check(SearchFirstName(edge).size()==63,"name truncation cannot split a UTF-16 surrogate pair");
    SYSTEMTIME now{};GetLocalTime(&now);Check(LocalSearchGreetingPeriod()==SearchGreetingPeriod(now.wHour),"greeting uses local device time");
    const auto& actual=UserSearchFirstName();Check(actual.size()<=64 && &actual==&UserSearchFirstName(),"local account name read once and bounded without logging identity");
    std::puts("PASS: all clock boundaries, three exact greetings/emojis, display-name extraction, fallback, Unicode capacity and cached local identity.");return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}}

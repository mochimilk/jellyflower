// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/search_text.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace acrylic;
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
unsigned readyMessages=0;
LRESULT CALLBACK ReadyWindow(HWND window,UINT message,WPARAM w,LPARAM l){if(message==SearchIdentityReadyMessage){++readyMessages;return 0;}return DefWindowProcW(window,message,w,l);}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
int main(int argc,char** argv){try{
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
    Check(SearchGivenName(L"  Mary Jane  ")==L"Mary Jane","explicit Windows given-name field keeps compound first names");
    Check(SearchGivenName(edge).size()==63 && SearchGivenName(L"\u3000Eunsoo\u3000")==L"Eunsoo","dedicated given name is trimmed and bounded safely");
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JellyflowerIdentityFixture";wc.lpfnWndProc=ReadyWindow;Check(RegisterClassW(&wc)!=0,"identity notification fixture registered");
    HWND window=CreateWindowExW(0,wc.lpszClassName,nullptr,0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);Check(window!=nullptr,"identity notification receiver created");
    const auto started=GetTickCount64();const std::wstring* actual=UserSearchFirstName(window);Check(GetTickCount64()-started<200,"starting identity lookup never blocks the UI thread");
    for(unsigned attempt=0;!actual && attempt<150;++attempt){Sleep(20);Pump();actual=UserSearchFirstName(window);}
    Check(actual && actual->size()<=64 && actual==UserSearchFirstName(),"one background account lookup publishes a stable bounded result");
    for(unsigned attempt=0;!readyMessages && attempt<100;++attempt){Sleep(10);Pump();}Check(readyMessages==1,"one completion message refreshes the controller without waiting for maintenance");
    for(unsigned attempt=0;attempt<10;++attempt)UserSearchFirstName(window);Pump();Check(readyMessages==1,"cached reads never repeat the request or notification");DestroyWindow(window);
    if(argc==3 && std::string_view(argv[1])=="--expect-first-name")Check(*actual==std::wstring(argv[2],argv[2]+std::char_traits<char>::length(argv[2])),"dedicated Windows first name matches the real expected given name");
    std::puts("PASS: all clock boundaries, three exact greetings/emojis, display-name extraction, fallback, Unicode capacity and cached local identity.");return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}}

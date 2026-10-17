// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only NLS and process-local translation checks; no Windows preferences change.
#include "../src/localization.hpp"
#include "../src/search_text.hpp"
#include "../src/clock_text.hpp"
#include "../src/menu_overflow.hpp"
#include "../src/wallpaper.hpp"
#include <set>
#include <cstdio>
#include <stdexcept>
using namespace acrylic;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::set<std::wstring> Placeholders(std::wstring_view text){
    std::set<std::wstring> result;
    for(size_t start=0;(start=text.find(L'{',start))!=text.npos;){
        const auto end=text.find(L'}',start);Check(end!=text.npos,"balanced translation placeholders");
        result.emplace(text.substr(start,end-start+1));start=end+1;
    }return result;
}
int main(){try{
    Language match{};
    Check(MatchLanguage(L"FR-ca",match) && match==Language::French,"French regional variants");
    Check(MatchLanguage(L"ko-KR",match) && match==Language::Korean,"Korean regional variant");
    Check(!MatchLanguage(L"french",match) && !MatchLanguage(L"korean",match),"whole language subtags only");
    constexpr wchar_t preferences[]=L"ja-JP\0fr-CA\0en-US\0\0";
    Check(SelectLanguage({preferences,std::size(preferences)})==Language::French,"first supported display preference");
    constexpr wchar_t englishFirst[]=L"en-GB\0ko-KR\0\0";
    Check(SelectLanguage({englishFirst,std::size(englishFirst)})==Language::English,"respect preferred order");
    Check(SelectLanguage(L"de-DE")==Language::English && SelectLanguage({})==Language::English,"unsupported and empty lists fall back");
    for(size_t id=1;id<Translations.size();++id){
        const auto fields=Placeholders(Translations[id][0]);
        for(unsigned lang=0;lang<3;++lang){
            Check(Translations[id][lang] && Translations[id][lang][0],"complete catalog in all three languages");
            Check(Placeholders(Translations[id][lang])==fields,"identical named placeholders in every language");
        }
    }
    Check(SearchGreeting(L"Anne-Marie",0,Language::French)==L"Bonjour, Anne-Marie 🤍","French morning and name");
    Check(SearchGreeting(L"은수",1,Language::Korean)==L"은수님, 좋은 오후예요 🧸","natural Korean greeting word order");
    Check(SearchGreeting({},2,Language::French)==L"Bonsoir 😇","anonymous French greeting");
    Check(SearchGreeting(L"{greeting}",0,Language::Korean)==L"{greeting}님, 좋은 아침이에요 🤍","name placeholders stay literal");
    for(unsigned lang=0;lang<3;++lang){
        const auto language=static_cast<Language>(lang);SetTextLanguage(language);
        for(unsigned period=0;period<3;++period)Check(SearchGreeting(std::wstring(64,L'x'),period).size()<SearchPlaceholderCapacity,"greetings fit immutable shared slots");
        const auto guide=CursorGuideText(L"C:\\사용자\\Élodie\\cursors");
        Check(guide.find(L"https://jepricreations.com/products/cursor-concept-3-hdpi")!=guide.npos && guide.find(L"C:\\사용자\\Élodie\\cursors")!=guide.npos,"localized guide preserves URL and Unicode path");
        Check(std::wstring_view(WallpaperLabel(WallpaperStatus::ErrorOff,E_FAIL)).find(L"80004005")!=std::wstring_view::npos,"localized errors retain numeric diagnostic code");
        Check(std::wstring_view(WallpaperLabel(WallpaperStatus::ErrorOff,E_ACCESSDENIED))==Tr(TextId::NotAllowed),"wallpaper statuses use selected language");
        const auto message=Text(TextId::SetupError,{{L"step",Tr(TextId::SetupExtracting)},{L"error",L"170"}});
        Check(message.find(L"170")!=message.npos && message.find(L'{')==message.npos,"translated installer formats step and code");
    }
    SYSTEMTIME sample{2026,6,1,22,9,41,0,0};
    Check(LocalizedClock(sample,true,L"en-US")==L"Mon Jun 22  9:41 am","English clock retains screenshot format");
    const auto french=LocalizedClock(sample,true,L"fr-FR");
    Check(french.find(L"juin")!=french.npos && french.find(L"22")<french.find(L"juin") && french.find(L"09:41")!=french.npos,"French regional month, date order and 24-hour clock");
    const auto korean=LocalizedClock(sample,true,L"ko-KR");
    Check(korean.find(L"6월 22일")!=korean.npos && korean.find(L"오전")!=korean.npos,"Korean date and day-period marker");
    Check(LocalizedClock(sample,false,L"fr-FR").find(L"juin")==std::wstring::npos,"date off stays time only");
    Check(MenuTickerOffset(899,100,25)==0 && MenuTickerOffset(2900,100,25)==50 && MenuTickerOffset(4900,100,25)==100 && MenuTickerOffset(5800,100,25)==0,"ticker pauses, travels, pauses and resets");
    DWORD pixels[24];for(auto& pixel:pixels)pixel=0x80402010;
    FadeMenuText(pixels,12,2,3,true,true);
    Check(pixels[0]==0 && pixels[11]==0 && pixels[5]==0x80402010 && pixels[1]==0x2a150a05,"edge fades preserve premultiplied alpha and center ink");
    SetTextLanguage(Language::English);
    std::puts("PASS: display-language matching/fallback, complete en/fr/ko catalog, placeholders, greetings, guide, installer/errors, regional clocks, overflow fade/ticker.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}

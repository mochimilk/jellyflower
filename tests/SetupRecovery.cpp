// SPDX-License-Identifier: GPL-3.0-or-later
// Production registration snapshots, with an in-memory registry. No account writes.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <map>
#include <vector>
#include <string>
#include <iostream>
struct TestValue { DWORD type; std::vector<BYTE> bytes; };
using TestKey = std::map<std::wstring,TestValue>;
std::map<std::wstring,TestKey> registry;
TestKey& Key(HKEY key) { return *reinterpret_cast<TestKey*>(key); }
LSTATUS TestOpen(HKEY,LPCWSTR path,DWORD,REGSAM,PHKEY key) {
    const auto found=registry.find(path);if(found==registry.end())return ERROR_FILE_NOT_FOUND;
    *key=reinterpret_cast<HKEY>(&found->second);return 0;
}
LSTATUS TestCreate(HKEY,LPCWSTR path,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY key,LPDWORD) {
    *key=reinterpret_cast<HKEY>(&registry[path]);return 0;
}
LSTATUS TestQuery(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size) {
    const auto found=Key(key).find(name);if(found==Key(key).end())return ERROR_FILE_NOT_FOUND;
    if(type)*type=found->second.type;
    const DWORD needed=static_cast<DWORD>(found->second.bytes.size());
    if(data && *size<needed){*size=needed;return ERROR_MORE_DATA;}
    if(data)memcpy(data,found->second.bytes.data(),needed);*size=needed;return 0;
}
LSTATUS TestEnum(HKEY key,DWORD index,LPWSTR name,LPDWORD length,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size) {
    if(index>=Key(key).size())return ERROR_NO_MORE_ITEMS;
    auto found=Key(key).begin();std::advance(found,index);
    if(*length<=found->first.size())return ERROR_MORE_DATA;
    wcscpy_s(name,*length,found->first.c_str());*length=static_cast<DWORD>(found->first.size());
    return TestQuery(key,found->first.c_str(),reserved,type,data,size);
}
LSTATUS TestSet(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD size) {Key(key)[name]={type,std::vector<BYTE>(data,data+size)};return 0;}
LSTATUS TestDeleteTree(HKEY,LPCWSTR path){return registry.erase(path)?0:ERROR_FILE_NOT_FOUND;}
LSTATUS TestDeleteValue(HKEY key,LPCWSTR name){return Key(key).erase(name)?0:ERROR_FILE_NOT_FOUND;}
LSTATUS TestClose(HKEY){return 0;}
#define RegOpenKeyExW TestOpen
#define RegCreateKeyExW TestCreate
#define RegQueryValueExW TestQuery
#define RegEnumValueW TestEnum
#define RegSetValueExW TestSet
#define RegDeleteTreeW TestDeleteTree
#define RegDeleteValueW TestDeleteValue
#define RegCloseKey TestClose
#define wWinMain SetupEntry
#include "../src/setup.cpp"
void Check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
TestValue String(const wchar_t* text){const auto bytes=reinterpret_cast<const BYTE*>(text);return {REG_SZ,std::vector<BYTE>(bytes,bytes+(wcslen(text)+1)*sizeof(wchar_t))};}
int main() {
    try {
        registry[RunKey][RunValue]=String(L"\"old jellyflower.exe\"");
        registry[RunKey][L"unrelated"]=String(L"other app");
        registry[UninstallKey][L"DisplayVersion"]=String(L"0.14.1");
        registry[UninstallKey][L"InstallLocation"]=String(L"old folder");
        const auto original=registry;
        RegistryBackup startup(RunKey,RunValue),registration(UninstallKey);
        registry[RunKey][RunValue]=String(L"\"new jellyflower.exe\"");
        registry[RunKey][L"unrelated"]=String(L"other app updated while setup ran");
        registry[UninstallKey][L"DisplayVersion"]=String(L"0.15.0");
        registry[UninstallKey][L"new value"]=String(L"new");
        startup.Restore();registration.Restore();
        Check(registry[RunKey][RunValue].bytes==original.at(RunKey).at(RunValue).bytes,"rollback restores old startup exactly");
        Check(registry[RunKey][L"unrelated"].bytes==String(L"other app updated while setup ran").bytes,"rollback does not overwrite other apps' startup changes");
        Check(registry[UninstallKey][L"DisplayVersion"].bytes==original.at(UninstallKey).at(L"DisplayVersion").bytes,"rollback restores prior version");
        Check(registry[UninstallKey].count(L"new value")==0,"rollback removes partial new registration");
        registry.erase(UninstallKey);RegistryBackup absent(UninstallKey);
        registry[UninstallKey][L"DisplayName"]=String(L"partial first install");absent.Restore();
        Check(registry.count(UninstallKey)==0,"failed first install removes only its new registration");
        std::cout<<"PASS: production setup snapshots restore startup/uninstall registration and retain unrelated app changes; no real registry writes.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

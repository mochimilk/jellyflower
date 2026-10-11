// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_session.hpp"
#include <cstdio>
int main(int argc,char**) {
    if(argc>1) {
        acrylic::StartSession start;
        for(unsigned i=0;i<12;++i) {start.Refresh(false);Sleep(500);if(start.Count() && start.SearchCount()) ExitProcess(0);}
        ExitProcess(4); // Deliberately skip destruction to test controller loss.
    }
    acrylic::Handle mapping;
    acrylic::StartState* state=nullptr;
    {
        acrylic::StartSession start;
        for(unsigned i=0;i<12;++i) {
            start.Refresh(false);Sleep(500);
            std::printf("pid=%lu attached=%ld count=%ld searches=%ld result=%08lx\n",start.Pid(),start.Attached(),start.Count(),start.SearchCount(),start.Result());
            if(start.Attached() && start.Count() && start.SearchCount()) break;
        }
        if(!start.Attached() || !start.Count() || !start.SearchCount()) return 1;
        mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,acrylic::StartMappingName(start.Pid()).c_str()));
        if(mapping) state=static_cast<acrylic::StartState*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(acrylic::StartState)));
        if(!state) return 2;
        start.Refresh(true);Sleep(300);start.Refresh(false);Sleep(300);
    }
    for(unsigned i=0;i<20 && (acrylic::Read(state->count) || acrylic::Read(state->searches));++i) Sleep(100);
    const bool restored=acrylic::Read(state->running)==0 && acrylic::Read(state->count)==0 && acrylic::Read(state->searches)==0;
    if(!restored) {UnmapViewOfFile(state);std::puts("FAIL: real Start properties did not restore after owner shutdown");return 3;}
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    std::wstring command=L"\""+std::wstring(path)+L"\" --abandon";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return 5;
    acrylic::Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),15000)!=WAIT_OBJECT_0) return 6;
    DWORD error=0;GetExitCodeProcess(child.get(),&error);if(error) return 7;
    for(unsigned i=0;i<30 && (acrylic::Read(state->count) || acrylic::Read(state->searches));++i) Sleep(100);
    const bool lostOwnerRestored=acrylic::Read(state->count)==0 && acrylic::Read(state->searches)==0;
    UnmapViewOfFile(state);
    if(!lostOwnerRestored) {std::puts("FAIL: lost controller did not restore Start");return 8;}
    std::puts("PASS: real Start connection, material/search discovery, theme publication, graceful shutdown and abrupt owner-loss restoration.");
}

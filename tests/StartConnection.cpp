// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/start_session.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv) {
    bool hold=false,search=false,abandon=false;
    for(int i=1;i<argc;++i){hold|=!std::strcmp(argv[i],"--hold");search|=!std::strcmp(argv[i],"--search");abandon|=!std::strcmp(argv[i],"--abandon");}
    const auto host=search?acrylic::ShellHost::Search:acrylic::ShellHost::Start;
    if(abandon) {
        acrylic::StartSession start(host);
        for(unsigned i=0;i<40;++i) {start.Refresh(false);Sleep(500);if(start.SearchCount() && (search || start.Count())) ExitProcess(0);}
        ExitProcess(4); // Deliberately skip destruction to test controller loss.
    }
    acrylic::Handle mapping;
    acrylic::StartState* state=nullptr;
    {
        acrylic::StartSession start(host);
        for(unsigned i=0;i<40;++i) {
            start.Refresh(false);Sleep(500);
            std::printf("pid=%lu attached=%ld count=%ld searches=%ld spacing=%ld result=%08lx\n",start.Pid(),start.Attached(),start.Count(),start.SearchCount(),start.SpacingCount(),start.Result());std::fflush(stdout);
            if(start.Attached()==1 && start.SearchCount() && (search || (start.Count() && start.SpacingCount()))) break;
        }
        if(start.Attached()!=1 || !start.SearchCount() || (!search && (!start.Count() || !start.SpacingCount()))) return 1;
        mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,acrylic::StartMappingName(start.Pid()).c_str()));
        if(mapping) state=static_cast<acrylic::StartState*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(acrylic::StartState)));
        if(!state) return 2;
        start.Refresh(true);Sleep(300);start.Refresh(false);Sleep(300);
        if(hold) Sleep(30000); // Optional bounded visual inspection of Start only.
    }
    for(unsigned i=0;i<20 && (acrylic::Read(state->count) || acrylic::Read(state->searches) || acrylic::Read(state->spacing));++i) Sleep(100);
    const bool restored=acrylic::Read(state->running)==0 && acrylic::Read(state->count)==0 && acrylic::Read(state->searches)==0 && acrylic::Read(state->spacing)==0;
    if(!restored) {UnmapViewOfFile(state);std::puts("FAIL: real Start properties did not restore after owner shutdown");return 3;}
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    std::wstring command=L"\""+std::wstring(path)+L"\" --abandon"+(search?L" --search":L"");
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return 5;
    acrylic::Handle child(process.hProcess),thread(process.hThread);
    if(WaitForSingleObject(child.get(),25000)!=WAIT_OBJECT_0) return 6;
    DWORD error=0;GetExitCodeProcess(child.get(),&error);if(error) return 7;
    for(unsigned i=0;i<30 && (acrylic::Read(state->count) || acrylic::Read(state->searches) || acrylic::Read(state->spacing));++i) Sleep(100);
    const bool lostOwnerRestored=acrylic::Read(state->count)==0 && acrylic::Read(state->searches)==0 && acrylic::Read(state->spacing)==0;
    UnmapViewOfFile(state);
    if(!lostOwnerRestored) {std::puts("FAIL: lost controller did not restore Start");return 8;}
    std::puts(search?"PASS: packaged Search connection, native input discovery, theme publication, graceful shutdown, controller rebind and abrupt owner-loss restoration.":"PASS: real Start connection, material/search discovery, theme publication, graceful shutdown and abrupt owner-loss restoration.");
}

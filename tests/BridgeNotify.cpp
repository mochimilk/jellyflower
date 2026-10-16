// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/bridge_notify.hpp"
#include <atomic>
#include <cstdio>
#include <thread>

acrylic::SharedState state{};
std::atomic<int> received[2]{}, observed[2]{}, running[2]{}, wrongThread{0};
std::atomic<DWORD> threadIds[2]{};
thread_local int index = 0;
int failures = 0;
void Check(bool value, const char* name) {
    if (!value) { ++failures; printf("FAIL: %s\n", name); }
}
void Refresh() noexcept {
    if (GetCurrentThreadId() != threadIds[index]) ++wrongThread;
    observed[index] = acrylic::Read(state.active);
    running[index] = acrylic::Read(state.running);
    ++received[index];
}
bool WaitForCount(int count) {
    const auto limit = GetTickCount64() + 1000;
    while ((received[0] < count || received[1] < count) && GetTickCount64() < limit) Sleep(1);
    return received[0] == count && received[1] == count;
}
int main() {
    using acrylic::AcrylicMode;
    Check(acrylic::NextAcrylicMode(AcrylicMode::System) == AcrylicMode::Pure
        && acrylic::NextAcrylicMode(AcrylicMode::Pure) == AcrylicMode::Off
        && acrylic::NextAcrylicMode(AcrylicMode::Off) == AcrylicMode::System, "system/pure/off cycle order");
    for (bool light : {false, true}) {
        const auto pure = acrylic::Recipe(AcrylicMode::Pure, light);
        Check(pure.tint == 0.0 && pure.luminosity == 0.0, "pure has no color or luminosity tint in either theme");
        Check(pure.legacyColor == 0x01000000u, "legacy pure keeps required one-step alpha");
    }
    const DWORD pid = GetCurrentProcessId();
    Check(!acrylic::OlderBridgePresent(pid), "clean Explorer needs no reload");
    {
        acrylic::Handle current(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, acrylic::MappingName(pid).c_str()));
        Check(current && !acrylic::OlderBridgePresent(pid), "current helper remains compatible");
        const auto name = L"Local\\AcrylicTaskbar.Bridge.v14." + std::to_wstring(pid);
        acrylic::Handle old(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, name.c_str()));
        Check(old && acrylic::OlderBridgePresent(pid), "old pinned material requires Explorer reload");
    }
    Check(!acrylic::OlderBridgePresent(pid), "reload clears incompatible helper");
    {
        const auto name=L"Local\\AcrylicTaskbar.Bridge.v15."+std::to_wstring(pid);
        acrylic::Handle old(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,name.c_str()));
        auto fields=static_cast<volatile LONG*>(MapViewOfFile(old.get(),FILE_MAP_ALL_ACCESS,0,0,14*sizeof(LONG)));
        Check(fields!=nullptr,"known v15 fixture mapping");
        if(fields){
            InterlockedExchange(&fields[0],0x4154423F);InterlockedExchange(&fields[8],1);
            Check(acrylic::OlderBridgePresent(pid),"still-running v15 cannot be replaced");
            InterlockedExchange(&fields[8],0);InterlockedExchange(&fields[9],1);
            Check(acrylic::OlderBridgePresent(pid),"stopped v15 with unrestored clock remains blocked");
            InterlockedExchange(&fields[9],0);
            Check(!acrylic::OlderBridgePresent(pid),"known stopped and restored v15 permits uniquely named v16");
            InterlockedExchange(&fields[0],0);
            Check(acrylic::OlderBridgePresent(pid),"unknown mapping contents cannot bypass reload guard");
            UnmapViewOfFile(const_cast<LONG*>(fields));
        }
    }
    HANDLE ready[2]{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> created[2]{};
    std::thread workers[2];
    for (int i = 0; i < 2; ++i) workers[i] = std::thread([&, i] {
        index = i;
        threadIds[i] = GetCurrentThreadId();
        const HWND window = acrylic::EnsureBridgeNotifyWindow(GetModuleHandleW(nullptr), Refresh);
        created[i] = window && !IsWindowVisible(window)
            && acrylic::EnsureBridgeNotifyWindow(GetModuleHandleW(nullptr), Refresh) == window;
        SetEvent(ready[i]);
        if (!window) return;
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        DestroyWindow(window);
    });
    Check(WaitForMultipleObjects(2, ready, TRUE, 5000) == WAIT_OBJECT_0 && created[0] && created[1],
        "one invisible receiver per UI thread, reused on repeated calls");
    if (created[0] && created[1]) {
        const auto start = GetTickCount64();
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::System);
        Check(WaitForCount(1) && observed[0] == 1 && observed[1] == 1, "on published before both UI-thread notifications");
        const auto elapsed = GetTickCount64() - start;
        Check(elapsed < 500, "notification dispatch bypasses the two-second recovery timer");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::System);
        Sleep(50);
        Check(received[0] == 1 && received[1] == 1, "unchanged active state produces no idle notification");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Off);
        Check(WaitForCount(2) && observed[0] == 0 && observed[1] == 0, "off and graceful release wake both UI threads");
        InterlockedIncrement(&state.epoch);
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Off, true);
        Check(WaitForCount(3), "new controller can notify even with unchanged active state");
        acrylic::SetBridgeMode(&state, pid + 1, acrylic::AcrylicMode::System);
        acrylic::SetBridgeMode(nullptr, pid, acrylic::AcrylicMode::Off);
        Sleep(50);
        Check(received[0] == 3 && received[1] == 3 && acrylic::Read(state.active) == 1,
            "receiver discovery filters process ID; null state is ignored");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::System, true);
        Check(WaitForCount(4), "forced same-state notification after reconnect");
        for (int n = 0; n < 40; ++n) acrylic::SetBridgeMode(&state, pid, n % 2 != 0 ? acrylic::AcrylicMode::System : acrylic::AcrylicMode::Off);
        Check(WaitForCount(44) && observed[0] == 1 && observed[1] == 1, "rapid toggles read the final published state");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Pure);
        Check(WaitForCount(45) && observed[0] == 2 && observed[1] == 2, "system to pure publishes material change without off transition");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Pure);
        Sleep(30);
        Check(received[0] == 45 && received[1] == 45, "unchanged pure produces no extra notifications");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Off);
        Check(WaitForCount(46) && observed[0] == 0 && observed[1] == 0, "pure to off restores through immediate notification");
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::System);
        Check(WaitForCount(47) && observed[0] == 1 && observed[1] == 1, "off to system completes cycle");
        InterlockedExchange(&state.running, 1);
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Off);
        Check(WaitForCount(48) && running[0] == 1 && running[1] == 1,
            "acrylic off does not end controller lifetime or clock ownership");
        InterlockedExchange(&state.running, 0);
        acrylic::SetBridgeMode(&state, pid, acrylic::AcrylicMode::Off, true);
        Check(WaitForCount(49) && running[0] == 0 && running[1] == 0,
            "exit wakes both UI threads even when acrylic is already off");
        Check(wrongThread == 0, "refresh callbacks stay on their owning UI thread");
        printf("Native notification dispatch to two UI threads: %llu ms.\n", static_cast<unsigned long long>(elapsed));
    }
    for (int i = 0; i < 2; ++i) {
        if (created[i]) PostThreadMessageW(threadIds[i], WM_QUIT, 0, 0);
        workers[i].join();
        CloseHandle(ready[i]);
    }
    printf("%s: publish ordering, prompt on/off dispatch, unchanged state, reconnect, process filtering, rapid toggles, and UI-thread affinity.\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

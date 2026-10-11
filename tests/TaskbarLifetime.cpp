// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/taskbar_probe.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace acrylic;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int wmain(int argc, wchar_t**) {
    if (argc > 1) { Sleep(INFINITE); return 0; }
    PROCESS_INFORMATION child{};
    HWND root = nullptr, nested = nullptr, island = nullptr, flyout = nullptr;
    try {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = instance;
        wc.lpszClassName = L"Windows.UI.Composition.DesktopWindowContentBridge";
        Check(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "register island fixture");
        root = CreateWindowW(L"STATIC", L"isolated taskbar fixture", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        Check(root != nullptr, "create taskbar fixture");
        Handle owner(OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId()));
        const auto pid = GetCurrentProcessId();
        Check(ProbeTaskbar(26200, root, nullptr, 0).backend == Unsupported, "unrecognized initial shell is untouched");
        nested = CreateWindowW(L"STATIC", nullptr, WS_CHILD, 0, 0, 1, 1, root, nullptr, instance, nullptr);
        island = CreateWindowW(wc.lpszClassName, L"DesktopWindowXamlSource", WS_CHILD, 0, 0, 1, 1, nested, nullptr, instance, nullptr);
        Check(island != nullptr, "create nested taskbar island");
        Check(ProbeTaskbar(26200, root, nullptr, 0).backend == Modern, "initial modern shell recognized");
        flyout = CreateWindowW(wc.lpszClassName, L"DesktopWindowXamlSource", WS_CHILD, 0, 0, 1, 1, root, nullptr, instance, nullptr);
        auto probe = ProbeTaskbar(26200, root, nullptr, 0);
        Check(probe.backend == Modern && probe.islands == 2, "multiple initial islands supported");
        for (int i = 0; i < 100; ++i) {
            probe = ProbeTaskbar(26200, root, owner.get(), pid);
            Check(probe.backend == Modern && probe.retained, "flyout must not disconnect live bridge");
            DestroyWindow(flyout); flyout = nullptr;
            DestroyWindow(island); island = nullptr;
            probe = ProbeTaskbar(26200, root, owner.get(), pid);
            Check(probe.backend == Modern && probe.islands == 0 && probe.retained, "temporary zero islands retain owner");
            Check(ProbeTaskbar(26200, nullptr, owner.get(), pid).backend == Modern, "missing taskbar during reconstruction retains owner");
            island = CreateWindowW(wc.lpszClassName, L"DesktopWindowXamlSource", WS_CHILD, 0, 0, 1, 1, nested, nullptr, instance, nullptr);
            flyout = CreateWindowW(wc.lpszClassName, L"DesktopWindowXamlSource", WS_CHILD, 0, 0, 1, 1, root, nullptr, instance, nullptr);
        }
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
        std::wstring command = L"\"" + std::wstring(path) + L"\" --child";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child) != FALSE, "start isolated process-lifetime fixture");
        probe = ProbeTaskbar(26200, root, child.hProcess, child.dwProcessId);
        Check(!probe.retained && probe.backend == Modern, "different Explorer PID requires fresh attachment");
        Check(ProbeTaskbar(26200, nullptr, child.hProcess, child.dwProcessId).retained, "live owner without HWND retained");
        Check(TerminateProcess(child.hProcess, 0) != FALSE, "stop isolated fixture");
        Check(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0, "fixture exited");
        Check(ProbeTaskbar(26200, nullptr, child.hProcess, child.dwProcessId).backend == Waiting, "dead Explorer releases ownership");
        Check(!ProbeTaskbar(26200, root, child.hProcess, pid).retained, "dead handle cannot retain even matching PID");
        Check(ProbeTaskbar(19045, root, nullptr, 0).backend == Legacy, "legacy backend unchanged");
        Check(ProbeTaskbar(16000, root, nullptr, 0).backend == Unsupported, "unsupported build unchanged");
        CloseHandle(child.hThread); CloseHandle(child.hProcess); child = {};
        DestroyWindow(root); root = nullptr;
        std::puts("PASS: 100 native HWND flyout/rebuild cycles, multiple initial islands, missing HWND retention, process replacement/death, reused-PID protection, legacy selection.");
        return 0;
    } catch (const std::exception& e) {
        if (child.hProcess) { TerminateProcess(child.hProcess, 1); CloseHandle(child.hThread); CloseHandle(child.hProcess); }
        if (root) DestroyWindow(root);
        std::printf("FAIL: %s\n", e.what()); return 1;
    }
}

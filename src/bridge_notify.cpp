// SPDX-License-Identifier: GPL-3.0-or-later
#include "bridge_notify.hpp"

namespace acrylic {
namespace {
struct Receiver { HWND window = nullptr; BridgeRefreshCallback refresh = nullptr; };
thread_local Receiver receiver;
LRESULT CALLBACK NotifyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == BridgeRefreshMessage) {
        if (receiver.refresh) receiver.refresh();
        return 0;
    }
    if (message == WM_NCDESTROY) receiver.window = nullptr;
    return DefWindowProcW(window, message, wParam, lParam);
}
}
HWND EnsureBridgeNotifyWindow(HINSTANCE module, BridgeRefreshCallback refresh) noexcept {
    if (receiver.window) return receiver.window;
    WNDCLASSW wc{};
    wc.hInstance = module;
    wc.lpfnWndProc = NotifyProc;
    wc.lpszClassName = BridgeNotifyClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    receiver.refresh = refresh;
    receiver.window = CreateWindowExW(0, BridgeNotifyClass, nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
    return receiver.window;
}
void SetBridgeMode(SharedState* state, DWORD explorerPid, AcrylicMode mode, bool forceNotify) noexcept {
    if (!state) return;
    const LONG desired = static_cast<LONG>(mode);
    if (InterlockedExchange(&state->active, desired) == desired && !forceNotify) return;
    if (!explorerPid) return;
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, BridgeNotifyClass, nullptr))) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == explorerPid) PostMessageW(window, BridgeRefreshMessage, 0, 0);
    }
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar_probe.hpp"
#include <iterator>
#include <cwchar>

namespace acrylic {
TaskbarProbe ProbeTaskbar(DWORD build, HWND primary, HANDLE ownerProcess, DWORD ownerPid) {
    unsigned islands = 0;
    DWORD pid = 0;
    if (primary) {
        GetWindowThreadProcessId(primary, &pid);
        if (!pid) primary = nullptr; // The HWND disappeared during discovery.
    }
    if (primary) {
        EnumChildWindows(primary, [](HWND child, LPARAM data) -> BOOL {
            wchar_t name[128]{}, title[128]{};
            GetClassNameW(child, name, static_cast<int>(std::size(name)));
            GetWindowTextW(child, title, static_cast<int>(std::size(title)));
            if (wcscmp(name, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0
                && wcscmp(title, L"DesktopWindowXamlSource") == 0)
                ++*reinterpret_cast<unsigned*>(data);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&islands));
    }
    // Flyouts and taskbar reconstruction can add/remove child islands. Their
    // count is evidence for initial attachment, never a reason to disconnect
    // a live Explorer that we already own. The XAML watcher tracks replacements.
    if (ownerPid && ownerProcess && WaitForSingleObject(ownerProcess, 0) == WAIT_TIMEOUT
        && (!primary || pid == ownerPid)) return {Modern, islands, true};
    if (!primary) return {Waiting, islands, false};
    if (build >= 22621) return {islands ? Modern : Unsupported, islands, false};
    if (build >= 17134) return {Legacy, islands, false};
    return {Unsupported, islands, false};
}
}

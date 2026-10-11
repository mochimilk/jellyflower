// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cwchar>

namespace {
DWORD values[2]{}, openError = 0, readError = 0;
bool exists[2]{true, true}, queueFails = false, rollbackFails = false;
int writes = 0, failWrite = 0, closed = 0, queued = 0;
LPTHREAD_START_ROUTINE notification = nullptr;
std::atomic<int> failures{0}, broadcasts{0};
DWORD mainThread = 0;
HANDLE completed = nullptr;
void Check(bool good, const char* name) { if (!good) { ++failures; std::printf("FAIL: %s\n", name); } }
int Index(LPCWSTR name) { return std::wcscmp(name, L"SystemUsesLightTheme") == 0 ? 0 : 1; }
LSTATUS FakeOpen(HKEY, LPCWSTR, DWORD, REGSAM rights, PHKEY key) {
    Check(rights == (KEY_QUERY_VALUE | KEY_SET_VALUE), "account preference access only");
    if (!openError) *key = reinterpret_cast<HKEY>(1);
    return openError;
}
LSTATUS FakeRead(HKEY, LPCWSTR, LPCWSTR name, DWORD, LPDWORD, PVOID data, LPDWORD bytes) {
    if (readError) return readError;
    const int i = Index(name);
    if (!exists[i]) return ERROR_FILE_NOT_FOUND;
    *static_cast<DWORD*>(data) = values[i]; *bytes = sizeof(DWORD); return ERROR_SUCCESS;
}
LSTATUS FakeWrite(HKEY, LPCWSTR name, DWORD, DWORD type, const BYTE* data, DWORD bytes) {
    ++writes;
    if (writes == failWrite || (rollbackFails && writes > failWrite)) return ERROR_ACCESS_DENIED;
    Check(type == REG_DWORD && bytes == sizeof(DWORD), "DWORD values");
    const int i = Index(name); values[i] = *reinterpret_cast<const DWORD*>(data); exists[i] = true;
    return ERROR_SUCCESS;
}
LSTATUS FakeDelete(HKEY, LPCWSTR name) {
    if (rollbackFails) return ERROR_ACCESS_DENIED;
    exists[Index(name)] = false; return ERROR_SUCCESS;
}
LSTATUS FakeClose(HKEY) { ++closed; return ERROR_SUCCESS; }
BOOL FakeQueue(LPTHREAD_START_ROUTINE callback, PVOID, ULONG) {
    ++queued; notification = callback;
    if (queueFails) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    return TRUE;
}
LRESULT FakeBroadcast(HWND window, UINT message, WPARAM w, LPARAM l, UINT flags, UINT timeout, PDWORD_PTR) {
    Check(GetCurrentThreadId() != mainThread, "broadcast runs outside menu thread");
    Check(window == HWND_BROADCAST && w == 0 && flags == SMTO_ABORTIFHUNG && timeout == 1000, "bounded native broadcast");
    Check((message == WM_SETTINGCHANGE && l && std::wcscmp(reinterpret_cast<LPCWSTR>(l), L"ImmersiveColorSet") == 0)
        || (message == WM_THEMECHANGED && l == 0), "theme notification messages");
    if (++broadcasts == 2) SetEvent(completed);
    return 1;
}
void Reset() {
    values[0] = values[1] = 0; exists[0] = exists[1] = true;
    openError = readError = 0; queueFails = rollbackFails = false;
    writes = failWrite = closed = queued = 0; notification = nullptr;
}
} // namespace
#define RegOpenKeyExW FakeOpen
#define RegGetValueW FakeRead
#define RegSetValueExW FakeWrite
#define RegDeleteValueW FakeDelete
#define RegCloseKey FakeClose
#define QueueUserWorkItem FakeQueue
#define SendMessageTimeoutW FakeBroadcast
#include "../src/theme.cpp"
#undef QueueUserWorkItem

int main() {
    mainThread = GetCurrentThreadId();
    Reset();
    auto result = acrylic::SetWindowsLightTheme(true);
    Check(!result.error && values[0] == 1 && values[1] == 1 && queued == 1 && closed == 1, "switch both to light");
    completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(QueueUserWorkItem(notification, nullptr, WT_EXECUTEDEFAULT)
        && WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0, "real worker completes notifications");
    CloseHandle(completed);
    result = acrylic::SetWindowsLightTheme(false);
    Check(!result.error && values[0] == 0 && values[1] == 0, "switch both to dark");
    Reset(); values[1] = 1;
    result = acrylic::SetWindowsLightTheme(true);
    Check(!result.error && values[0] == 1 && values[1] == 1, "mixed system/app preference becomes one mode");
    Reset(); openError = ERROR_ACCESS_DENIED;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error == ERROR_ACCESS_DENIED && !writes && !queued && !closed, "open failure changes nothing");
    Reset(); readError = ERROR_ACCESS_DENIED;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error && !writes && !queued && closed == 1, "read failure changes nothing and closes handle");
    Reset(); values[1] = 2;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error == ERROR_INVALID_DATA && !writes && !queued, "invalid existing preference is preserved");
    Reset(); failWrite = 1;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error && values[0] == 0 && values[1] == 0 && !queued, "first write failure changes nothing");
    Reset(); failWrite = 2;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error && !result.rollbackError && values[0] == 0 && values[1] == 0 && !queued, "second failure rolls system back");
    Reset(); exists[0] = false; failWrite = 2;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error && !result.rollbackError && !exists[0] && !queued, "rollback restores missing value");
    Reset(); failWrite = 2; rollbackFails = true;
    result = acrylic::SetWindowsLightTheme(true);
    Check(result.error && result.rollbackError && values[0] == 1 && queued == 1, "partial state is reported and notified");
    Reset(); queueFails = true;
    result = acrylic::SetWindowsLightTheme(true);
    Check(!result.error && result.notificationError == ERROR_NOT_ENOUGH_MEMORY && values[0] == 1 && values[1] == 1, "notification failure retains successful preference");
    Reset(); exists[0] = exists[1] = false;
    result = acrylic::SetWindowsLightTheme(true);
    Check(!result.error && exists[0] && exists[1] && values[0] == 1 && values[1] == 1, "missing values initialize together");
    std::printf("%s: theme writes, rollback, failures, and worker notifications; desktop registry/broadcasts simulated.\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

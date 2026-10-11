#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstring>
std::wstring pack;
DWORD savedMode = 0;
bool testLight = false, saveFails = false;
bool noSavedMode = false;
int calls = 0, restores = 0, failAt = -1, failures = 0;
uint64_t arrow = 0;
uint64_t Pixels(HCURSOR cursor) {
    ICONINFO icon{};
    if (!cursor || !GetIconInfo(cursor, &icon)) return 0;
    uint64_t hash = 1469598103934665603ULL;
    for (HBITMAP bitmap : {icon.hbmColor, icon.hbmMask}) {
        if (!bitmap) continue;
        BITMAP size{};
        GetObjectW(bitmap, sizeof(size), &size);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.bmWidth, -size.bmHeight, 1, 32};
        std::vector<BYTE> pixels(static_cast<size_t>(size.bmWidth) * size.bmHeight * 4);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, bitmap, 0, size.bmHeight, pixels.data(), &info, DIB_RGB_COLORS))
            for (BYTE value : pixels) hash = (hash ^ value) * 1099511628211ULL;
        else hash = 0;
        ReleaseDC(nullptr, dc);
        DeleteObject(bitmap);
    }
    return hash;
}
uint64_t FilePixels(const std::wstring& path) {
    HCURSOR cursor = LoadCursorFromFileW(path.c_str());
    const auto hash = Pixels(cursor);
    if (cursor) DestroyCursor(cursor);
    return hash;
}
LSTATUS FakeRead(HKEY, LPCWSTR, LPCWSTR name, DWORD, LPDWORD, PVOID value, LPDWORD bytes) {
    if (noSavedMode && wcscmp(name, L"CursorMode") == 0) return ERROR_FILE_NOT_FOUND;
    if (wcscmp(name, L"CursorPack") == 0) {
        const DWORD required = static_cast<DWORD>((pack.size() + 1) * sizeof(wchar_t));
        if (!value) { *bytes = required; return 0; }
        if (*bytes < required) return ERROR_MORE_DATA;
        memcpy(value, pack.c_str(), required); *bytes = required; return 0;
    }
    if (!value || *bytes < sizeof(DWORD)) return ERROR_MORE_DATA;
    *static_cast<DWORD*>(value) = wcscmp(name, L"CursorMode") == 0 ? savedMode : testLight;
    *bytes = sizeof(DWORD); return 0;
}
LSTATUS FakeCreate(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const SECURITY_ATTRIBUTES*, PHKEY key, LPDWORD) {
    if (saveFails) return ERROR_ACCESS_DENIED;
    *key = reinterpret_cast<HKEY>(1); return 0;
}
LSTATUS FakeSave(HKEY, LPCWSTR name, DWORD, DWORD, const BYTE* value, DWORD) {
    if (wcscmp(name, L"CursorMode") == 0) memcpy(&savedMode, value, sizeof(savedMode));
    return 0;
}
LSTATUS FakeClose(HKEY) { return 0; }
BOOL FakeSet(HCURSOR cursor, DWORD id) {
    const int current = calls++;
    if (id == 32512) arrow = Pixels(cursor);
    DestroyCursor(cursor); // Same ownership transfer as SetSystemCursor.
    if (current == failAt) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return TRUE;
}
BOOL FakeRestore(UINT action, UINT, PVOID, UINT) { if (action == SPI_SETCURSORS) ++restores; return TRUE; }
#define RegGetValueW FakeRead
#define RegCreateKeyExW FakeCreate
#define RegSetValueExW FakeSave
#define RegCloseKey FakeClose
#define SetSystemCursor FakeSet
#define SystemParametersInfoW FakeRestore
#include "../src/cursors.cpp"
void Check(bool ok, const char* label) { if (!ok) { ++failures; printf("FAIL: %s\n", label); } }
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 87;
    pack = argv[1];
    {
        noSavedMode = true;
        const int before = calls, beforeRestore = restores;
        acrylic::CursorManager fresh(L""); fresh.Refresh();
        Check(fresh.Mode() == acrylic::CursorDisabled && fresh.Available() == (1u << 11) - 1
            && calls == before && restores == beforeRestore, "fresh install with an available pack still preserves Windows cursor");
        noSavedMode = false;
    }
    {
        acrylic::CursorManager manager(L"");
        manager.Refresh();
        Check(manager.Applied() && arrow == FilePixels(pack + L"\\cursor\\assets\\dark\\arrow.cur"), "default resolves dark");
        testLight = true; manager.Refresh();
        Check(arrow == FilePixels(pack + L"\\cursor\\assets\\light\\arrow.cur"), "default follows theme change");
        for (UINT mode = 1; mode <= 9; ++mode) {
            const int before = calls;
            Check(manager.Select(mode) == 0 && calls == before + 17 && savedMode == mode, "all roles applied and selection saved");
            Check(arrow == FilePixels(pack + L"\\cursor\\assets\\" + acrylic::CursorColors[mode - 1].folder + L"\\arrow.cur"), "loaded cursor pixels match chosen variant");
        }
        const int beforeTheme = calls;
        testLight = false; manager.Refresh();
        Check(calls == beforeTheme, "explicit color survives theme change");
        Check(manager.Select(acrylic::CursorDisabled) == 0 && savedMode == acrylic::CursorDisabled, "disable saved");
        Check(arrow == FilePixels(L"C:\\Windows\\Cursors\\aero_arrow.cur"), "disable loads Microsoft stock arrow");
    }
    Check(restores > 0, "exit reloads Windows scheme");
    {
        acrylic::CursorManager manager(L"");
        Check(manager.Mode() == acrylic::CursorDisabled, "saved selection read on next launch");
        manager.Refresh();
        const int beforeReset = restores;
        failAt = calls + 3;
        Check(manager.Select(3) == ERROR_INVALID_PARAMETER && !manager.Applied() && restores == beforeReset + 1,
            "partial apply failure reloads Windows and reports error");
        Check(savedMode == acrylic::CursorDisabled, "failed selection does not persist");
        failAt = -1; saveFails = true;
        Check(manager.Select(3) == ERROR_ACCESS_DENIED && manager.Applied(), "preference save failure distinguished from apply failure");
        saveFails = false;
    }
    pack += L"\\does-not-exist";
    {
        noSavedMode = true;
        const int before = calls, beforeRestore = restores;
        acrylic::CursorManager fresh(L"");
        fresh.Refresh();
        Check(fresh.Mode() == acrylic::CursorDisabled && !fresh.Applied() && fresh.Error() == ERROR_SUCCESS
            && calls == before && restores == beforeRestore, "fresh computer without optional pack preserves configured Windows cursors quietly");
        noSavedMode = false;
    }
    {
        acrylic::CursorManager manager(L"");
        const int before = calls;
        Check(manager.Select(3) != 0 && calls == before, "missing variant makes no partial system changes");
        Check(manager.Available() == (1u << acrylic::CursorDisabled), "missing pack locks every custom mode but leaves disable available");
    }
    printf("%s: real file/bitmap loading; simulated system writes for theme, nine colors, stock disable, preferences, cleanup, and failure rollback.\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

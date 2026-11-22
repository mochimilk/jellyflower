// SPDX-License-Identifier: GPL-3.0-or-later
// Live check of the compact display frame on this desktop. Needs a display
// narrower than Start (compact.hpp) and changes the real work area while it
// runs, so it is not part of CI. Run it with no other jellyflower controller
// changing app bars:
//   CompactScreen.exe [capture.bmp [top-bar.bmp]]
// It frames the compact displays, checks every margin through the monitor's
// work area, optionally saves the top band (status bar included), turns the
// status bar off and on, turns the top bar on (every display) and off again,
// then removes the frame and checks the work area is back.
#include "../src/compact.hpp"
#include "../src/compact_screen.hpp"
#include "../src/top_bar.hpp"
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace acrylic;

namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
void Pump(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
}
struct Display { HMONITOR handle; RECT monitor, work; UINT dpi; bool compact; };
std::vector<Display> AllDisplays() {
    std::vector<Display> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{sizeof(info)};
        UINT dpi = 0, unused = 0;
        if (GetMonitorInfoW(monitor, &info) && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &unused)))
            reinterpret_cast<std::vector<Display>*>(data)->push_back({monitor, info.rcMonitor, info.rcWork, dpi,
                IsCompactDisplay(info.rcMonitor.right - info.rcMonitor.left, dpi)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::vector<Display> Compact() {
    auto result = AllDisplays();
    result.erase(std::remove_if(result.begin(), result.end(), [](const Display& display) { return !display.compact; }), result.end());
    return result;
}
// This process's top edge on a display (or on any, for an empty monitor). A
// running jellyflower's own bar can sit above it: app bars stack.
HWND OwnTopEdge(const RECT& monitor = {}) {
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, L"AcrylicTaskbar.ScreenEdge.v1", L"jellyflower status bar")) != nullptr) {
        DWORD pid = 0;
        RECT bounds{}, inside{};
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && GetWindowRect(window, &bounds)
            && (IsRectEmpty(&monitor) || (IntersectRect(&inside, &bounds, &monitor) && EqualRect(&inside, &bounds)))) return window;
    }
    return nullptr;
}
RECT Work(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}
std::wstring Text(const RECT& r) {
    return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom);
}
HWND StatusBar() { return OwnTopEdge(); }
// CAPTUREBLT includes layered windows such as the status bar.
void Capture(const RECT& area, const wchar_t* path) {
    const int width = area.right - area.left, height = area.bottom - area.top;
    const HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, height, 1, 24, BI_RGB};
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const auto previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, area.left, area.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    const DWORD stride = (static_cast<DWORD>(width) * 3 + 3) & ~3u, size = stride * static_cast<DWORD>(height);
    BITMAPFILEHEADER header{0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + size, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, &header, sizeof(header), &written, nullptr);
        WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr);
        WriteFile(file, bits, size, &written, nullptr);
        CloseHandle(file);
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        const auto displays = Compact();
        Check(!displays.empty(), "no display is narrower than Start; nothing to frame");
        CompactScreen screen;
        screen.Refresh({true, false, true, false});
        Pump(1500);
        Check(screen.Displays() == static_cast<LONG>(displays.size()) && screen.Result() == S_OK, "every compact display is framed without an error");
        for (const auto& display : displays) {
            const RECT work = Work(display.handle);
            std::wprintf(L"monitor %ls dpi %u: work %ls -> %ls\n", Text(display.monitor).c_str(), display.dpi, Text(display.work).c_str(), Text(work).c_str());
            const int side = CompactEdgePixels(ABE_LEFT, display.dpi), top = CompactEdgePixels(ABE_TOP, display.dpi);
            // Relative to the work area before, so another running frame only stacks.
            Check(work.top == display.work.top + top, "the top margin is the status bar's height");
            Check(work.left == display.work.left + side && work.right == display.work.right - side, "both sides keep a margin");
            Check(work.bottom == display.work.bottom - side, "a margin sits above the taskbar");
        }
        const HWND bar = StatusBar();
        RECT bounds{};
        Check(bar && IsWindowVisible(bar) && GetWindowRect(bar, &bounds), "the status bar is shown");
        std::wprintf(L"status bar %ls\n", Text(bounds).c_str());
        // The time and chevron take clicks; the rest of the bar lets them through.
        const POINT time{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        const POINT margin{bounds.left + (bounds.right - bounds.left) / 8, time.y};
        Check(WindowFromPoint(time) == bar, "the menu button over the time takes clicks");
        Check(WindowFromPoint(margin) != bar, "clicks elsewhere in the status bar fall through");
        if (argc > 1) {
            RECT band = bounds;
            band.bottom = band.top + 2 * CompactEdgePixels(ABE_TOP, displays[0].dpi);
            Capture(band, argv[1]);
        }
        const RECT framed = Work(displays[0].handle);
        screen.Refresh({false, false, true, false});
        Pump(300);
        Check(!IsWindowVisible(StatusBar()), "turning the status bar off hides the time");
        const RECT unchanged = Work(displays[0].handle);
        Check(EqualRect(&framed, &unchanged) != FALSE, "turning it off keeps the top margin, so windows do not move");
        screen.Refresh({true, false, true, false});
        Pump(300);
        Check(IsWindowVisible(StatusBar()), "turning it back on shows the time again");

        // The top bar takes the status bar's place in a compact display's top
        // margin and a strip of its own from every other display.
        const auto every = AllDisplays();
        screen.Refresh({true, true, true, false});
        Pump(1500);
        Check(screen.TopBars() == static_cast<LONG>(every.size()) && screen.Result() == S_OK, "every display shows the top bar without an error");
        for (const auto& display : every) {
            const RECT work = Work(display.handle);
            std::wprintf(L"top bar on %ls: work %ls -> %ls\n", Text(display.monitor).c_str(), Text(display.work).c_str(), Text(work).c_str());
            if (display.compact) Check(EqualRect(&work, &display.work) != FALSE, "a compact display keeps its margins");
            else Check(work.top == display.work.top + TopEdgePixels(false, display.dpi) && work.bottom == display.work.bottom,
                "another display gives the top bar 32 DIP at the top");
        }
        // The flower and the clock take clicks; the rest of the bar lets them through.
        const auto& first = displays[0];
        const HWND top = OwnTopEdge(first.monitor);
        RECT strip{};
        Check(top && IsWindowVisible(top) && GetWindowRect(top, &strip), "the compact display shows the top bar");
        std::wprintf(L"top bar %ls\n", Text(strip).c_str());
        const float scale = static_cast<float>(first.dpi) / 96;
        const LONG y = (strip.top + strip.bottom) / 2;
        const POINT flower{strip.left + std::lround((TopBarEdge + StatusButtonPadding + TopBarIconSize / 2) * scale), y};
        const POINT clock{strip.right - std::lround((TopBarEdge + StatusButtonPadding) * scale), y};
        const POINT middle{(strip.left + strip.right) / 2, y};
        Check(WindowFromPoint(flower) == top && WindowFromPoint(clock) == top, "the flower and the clock take clicks");
        Check(WindowFromPoint(middle) != top, "clicks between them fall through");
        if (argc > 2) {
            RECT band = strip;
            band.bottom = band.top + 2 * (strip.bottom - strip.top);
            Capture(band, argv[2]);
        }
        screen.Refresh({true, false, true, false});
        Pump(500);
        Check(screen.TopBars() == 0 && IsWindowVisible(StatusBar()), "turning the top bar off brings the status bar back");
        for (const auto& display : every) {
            const RECT work = Work(display.handle);
            Check(EqualRect(&work, &display.work) != FALSE, "turning the top bar off gives other displays their work area back");
        }
        screen.Remove();
        Pump(500);
        for (const auto& display : displays) {
            const RECT restored = Work(display.handle);
            std::wprintf(L"restored work %ls\n", Text(restored).c_str());
            Check(EqualRect(&display.work, &restored) != FALSE, "removing the frame restores the stock work area");
        }
        Check(!IsWindow(bar), "removing the frame destroys its windows");
        std::puts("CompactScreen passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CompactScreen failed: %s\n", error.what());
        return 1;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// FlutedLayout [capture.bmp]: the taskbar's fluted glass, from the helper's slot to the controller's window.
#include "../src/dock.hpp"
#include "../src/fluted_reeds.hpp"
#include "../src/glass_state.hpp"
#include "../src/taskbar_glass.hpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace shapes = xaml::Shapes;
namespace hosting = xaml::Hosting;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Pump(DWORD milliseconds = 0) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (milliseconds) Sleep(15);
    } while (GetTickCount64() < end);
}
bool Near(const RECT& a, const RECT& b) {
    return std::abs(a.left - b.left) <= 1 && std::abs(a.top - b.top) <= 1 && std::abs(a.right - b.right) <= 1 && std::abs(a.bottom - b.bottom) <= 1;
}
RECT Bounds(HWND window) { RECT rect{}; GetWindowRect(window, &rect); return rect; }
// This process's glass window: a running jellyflower has its own.
HWND Glass() {
    for (HWND window = nullptr; (window = FindWindowExW(nullptr, window, acrylic::TaskbarGlass::WindowClass, nullptr)) != nullptr;) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId()) return window;
    }
    return nullptr;
}
bool Topmost(HWND window) { return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }
// The screen's pixels in a rectangle: composition draws there, not into a window's DC.
std::vector<DWORD> Capture(const RECT& bounds, const wchar_t* path) {
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    const HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const auto old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    std::vector<DWORD> pixels(static_cast<const DWORD*>(bits), static_cast<const DWORD*>(bits) + static_cast<size_t>(width) * height);
    FILE* file = nullptr;
    if (path && _wfopen_s(&file, path, L"wb") == 0 && file) {
        BITMAPFILEHEADER header{0x4d42, 0, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size() * 4);
        fwrite(&header, sizeof(header), 1, file); fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file);
        fwrite(pixels.data(), 4, pixels.size(), file); fclose(file);
    }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    return pixels;
}
// Colored stripes stand in for the desktop behind the taskbar.
LRESULT CALLBACK Stripes(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message != WM_PAINT) return DefWindowProcW(window, message, wParam, lParam);
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(window, &paint);
    RECT bounds{}; GetClientRect(window, &bounds);
    const COLORREF colors[]{RGB(236, 64, 122), RGB(255, 193, 7), RGB(38, 166, 154), RGB(66, 133, 244), RGB(250, 250, 250), RGB(20, 20, 20)};
    for (int i = 0, x = 0; x < bounds.right; ++i, x += 60) {
        const RECT band{x, 0, x + 60, bounds.bottom};
        const HBRUSH brush = CreateSolidBrush(colors[i % 6]);
        FillRect(dc, &band, brush);
        DeleteObject(brush);
    }
    EndPaint(window, &paint);
    return 0;
}
LRESULT CALLBACK Plain(HWND window, UINT message, WPARAM wParam, LPARAM lParam) { return DefWindowProcW(window, message, wParam, lParam); }
// A test app's XAML island is opaque, unlike Explorer's see-through taskbar, so the taskbar is made fully
// transparent to look at the glass behind it, keeping its place in the z-order.
void SeeThrough(HWND bar, bool clear) {
    const LONG_PTR style = GetWindowLongPtrW(bar, GWL_EXSTYLE);
    SetWindowLongPtrW(bar, GWL_EXSTYLE, clear ? style | WS_EX_LAYERED : style & ~WS_EX_LAYERED);
    if (clear) SetLayeredWindowAttributes(bar, 0, 1, LWA_ALPHA);
}

// The helper's side: where a background is, relative to the taskbar window that holds its island.
void Helper() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"Shell_TrayWnd", nullptr,
        WS_POPUP | WS_VISIBLE, 100, 600, 600, 64, nullptr, nullptr, instance, nullptr);
    Check(bar != nullptr, "helper's taskbar window");
    hosting::DesktopWindowXamlSource source;
    winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->AttachToWindow(bar));
    HWND island = nullptr; winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&island));
    SetWindowPos(island, nullptr, 0, 0, 600, 64, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    {
        controls::Grid content;
        shapes::Rectangle fill; fill.Name(L"BackgroundFill");
        fill.Margin({40, 2, 40, 2}); fill.RadiusX(20); fill.RadiusY(20);
        content.Children().Append(fill);
        source.Content(content);
        content.UpdateLayout(); Pump(300);
        const double scale = content.XamlRoot().RasterizationScale();
        const RECT pill{std::lround(40 * scale), std::lround(2 * scale), 600 - std::lround(40 * scale), 64 - std::lround(2 * scale)};
        RECT bounds{};
        double found = 0;
        HRESULT error = E_FAIL;
        Check(acrylic::TaskbarBounds(fill, bounds, found, error) == bar && error == S_OK && found == scale && Near(bounds, pill),
            "the background's place relative to its taskbar window");
        shapes::Rectangle loose;
        Check(!acrylic::TaskbarBounds(loose, bounds, found, error) && FAILED(error), "an element in no taskbar has no place");
        source.Content(nullptr);
    }
    source.Close(); Pump(200);
    DestroyWindow(bar);
}

// The controller's side, on a thread without XAML, as in jellyflower.exe: a window of glass where a slot says.
void Controller(const wchar_t* capture) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND behind = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"FlutedLayoutStripes", nullptr,
        WS_POPUP | WS_VISIBLE, 40, 60, 1400, 400, nullptr, nullptr, instance, nullptr);
    const HWND bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"Shell_TrayWnd", nullptr,
        WS_POPUP | WS_VISIBLE, 100, 100, 600, 64, nullptr, nullptr, instance, nullptr);
    Check(behind && bar, "controller's fixture windows");
    UpdateWindow(behind);
    const double scale = GetDpiForWindow(bar) / 96.0;
    const auto pill = [&](LONG x, LONG y) {
        return RECT{x + std::lround(40 * scale), y + std::lround(2 * scale), x + 600 - std::lround(40 * scale), y + 64 - std::lround(2 * scale)};
    };
    acrylic::SharedState state{};
    const DWORD pid = GetCurrentProcessId();
    const acrylic::GlassSlot slot{bar, std::lround(40 * scale), std::lround(2 * scale), 600 - 2 * std::lround(40 * scale), 64 - 2 * std::lround(2 * scale), std::lround(20 * scale)};
    acrylic::WriteGlassSlot(state, 0, slot);
    {
        acrylic::TaskbarGlass glass;
        Check(glass.Refresh(&state, pid, false) && acrylic::GlassShown(state, 0) && glass.Error() == S_OK, "the glass goes up and says so");
        const HWND window = Glass();
        Check(window && IsWindowVisible(window) && Near(Bounds(window), pill(100, 100)), "the glass sits over the background's place on screen");
        Check(GetWindow(bar, GW_HWNDNEXT) == window && Topmost(window), "directly below the topmost taskbar");
        Check(GetForegroundWindow() != window && SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(300, 130)) == HTTRANSPARENT, "the glass never takes focus or input");
        Check(!glass.Refresh(&state, pid, false) && Glass() == window, "a refresh with nothing new changes nothing");

        SeeThrough(bar, true);
        Pump(700);
        const RECT shown = Bounds(window);
        const auto pixels = Capture(shown, capture);
        const int width = shown.right - shown.left, row = (shown.bottom - shown.top) / 2 * width;
        const auto step = [&](int x) {
            int sum = 0;
            for (int shift : {0, 8, 16}) sum += std::abs(static_cast<int>((pixels[row + x - 2] >> shift) & 255) - static_cast<int>((pixels[row + x + 1] >> shift) & 255));
            return sum;
        };
        const auto reeds = acrylic::FlutedReeds(width, static_cast<unsigned>(std::lround(96 * scale)));
        double seams = 0, middles = 0;
        for (size_t r = 1; r + 1 < reeds.size(); ++r) {
            seams += step(static_cast<int>(std::lround(reeds[r].left)));
            middles += step(static_cast<int>(std::lround(reeds[r].left + reeds[r].width / 2)));
        }
        Check(reeds.size() > 4 && seams > 2 * middles, "each reed bends the stripes behind the taskbar on its own");
        Check((pixels[0] & 0xffffff) != 0 && (pixels[width - 1] & 0xffffff) != 0, "the rounded corners are see-through, not black");

        // A window that opens behind the glass shows through it: the glass takes a new snapshot.
        // Larger than the glass on every side, so Windows' blur of the backdrop has only red to mix.
        const HWND red = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"FlutedLayoutRed", nullptr, WS_POPUP, shown.left - 300, shown.top - 300,
            shown.right - shown.left + 600, shown.bottom - shown.top + 600, nullptr, nullptr, instance, nullptr);
        // How far the glass leans red: its mean red over green.
        const auto redness = [&] {
            const auto seen = Capture(shown, nullptr);
            double sum = 0;
            for (const DWORD pixel : seen) sum += static_cast<double>((pixel >> 16) & 255) - static_cast<double>((pixel >> 8) & 255);
            return sum / seen.size();
        };
        // Windows refreshes the snapshot a new brush gets on its own schedule, and may take it mid
        // fade-in: the glass's own snapshots and the controller's refresh, every few seconds, bring
        // the window's color through.
        const double before = redness();
        SetWindowPos(red, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        bool through = false;
        for (int wait = 0; wait < 50 && !through; ++wait) {
            Pump(100);
            if (wait == 25) glass.Refresh(&state, pid, false);
            through = redness() > before + 12;
        }
        Check(GetWindow(bar, GW_HWNDNEXT) == window, "the glass stays directly below the taskbar");
        SeeThrough(bar, false);
        DestroyWindow(red);
        Check(through, "a window opened behind the glass shows through it");

        glass.Refresh(&state, pid, false);
        Check(GetWindow(bar, GW_HWNDNEXT) == window, "back directly below the taskbar");
        SetWindowPos(bar, nullptr, 160, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(200);
        Check(Near(Bounds(window), pill(160, 300)), "the glass follows the taskbar window's moves");
        acrylic::WriteGlassSlot(state, 0, {bar, slot.left + 30, slot.top, slot.width - 60, slot.height, slot.corner});
        glass.Refresh(&state, pid, false);
        const RECT moved = pill(160, 300), narrower{moved.left + 30, moved.top, moved.right - 30, moved.bottom};
        Check(Near(Bounds(window), narrower), "and a pill that grows or shrinks");
        acrylic::WriteGlassSlot(state, 0, slot);

        SetWindowPos(bar, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        glass.Refresh(&state, pid, false);
        Check(GetWindow(bar, GW_HWNDNEXT) == window && !Topmost(window), "a taskbar dropped below a full-screen app takes the glass with it");
        SetWindowPos(bar, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        glass.Refresh(&state, pid, false);
        Check(GetWindow(bar, GW_HWNDNEXT) == window && Topmost(window), "and back up");
        ShowWindow(bar, SW_HIDE);
        Check(glass.Refresh(&state, pid, false) && !acrylic::GlassShown(state, 0) && !IsWindowVisible(window), "hidden with the taskbar, and said so");
        ShowWindow(bar, SW_SHOWNA);
        Check(glass.Refresh(&state, pid, false) && acrylic::GlassShown(state, 0) && IsWindowVisible(window), "shown with it again");
        acrylic::WriteGlassSlot(state, 0, {});
        Check(glass.Refresh(&state, pid, false) && !acrylic::GlassShown(state, 0) && !Glass(), "an emptied slot takes the glass away");
        acrylic::WriteGlassSlot(state, 0, slot);
        glass.Refresh(&state, pid, false);
        Check(Glass() != nullptr, "on again");
        glass.Remove(&state);
        Check(!Glass() && !acrylic::GlassShown(state, 0), "remove takes every glass away");
        glass.Refresh(&state, pid, false);
    }
    Check(!Glass(), "destruction takes the glass away");
    DestroyWindow(bar); DestroyWindow(behind); Pump();
}

void Run(const wchar_t* capture) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW stripesClass{}; stripesClass.hInstance = instance; stripesClass.lpfnWndProc = Stripes; stripesClass.lpszClassName = L"FlutedLayoutStripes";
    WNDCLASSW barClass{}; barClass.hInstance = instance; barClass.lpfnWndProc = Plain; barClass.lpszClassName = L"Shell_TrayWnd";
    WNDCLASSW redClass{}; redClass.hInstance = instance; redClass.lpfnWndProc = Plain; redClass.lpszClassName = L"FlutedLayoutRed";
    redClass.hbrBackground = CreateSolidBrush(RGB(255, 0, 0));
    Check(RegisterClassW(&stripesClass) && RegisterClassW(&barClass) && RegisterClassW(&redClass), "fixture classes");
    Helper();
    std::string failure;
    std::thread([&] {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        try { Controller(capture); }
        catch (const std::exception& error) { failure = error.what(); }
        catch (const winrt::hresult_error& error) { failure = "HRESULT " + std::to_string(static_cast<unsigned>(error.code())); }
        winrt::uninit_apartment();
    }).join();
    Check(failure.empty(), failure.c_str());
    std::puts("PASS: fluted glass from the helper's slot (the background's place relative to its taskbar window) to the controller's window: over that place, directly below the taskbar in or out of the topmost band, bending what's behind with see-through corners, showing a window that opens behind it, following moves and the pill, hidden with the taskbar, never focused or hit, gone when the slot empties, on remove and on destruction.");
}
int wmain(int argc, wchar_t** argv) {
    // Window and screen pixels must agree for the capture.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    try { Run(argc > 1 ? argv[1] : nullptr); return 0; }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); }
    catch (const winrt::hresult_error& error) { std::printf("FAIL: HRESULT 0x%08x %ls\n", static_cast<unsigned>(error.code()), error.message().c_str()); }
    return 1;
}

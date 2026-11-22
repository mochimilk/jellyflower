// SPDX-License-Identifier: GPL-3.0-or-later
#include "compact_screen.hpp"
#include "compact.hpp"
#include "top_bar.hpp"
#include "clock_text.hpp"
#include "resource.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace acrylic {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t EdgeClass[] = L"AcrylicTaskbar.ScreenEdge.v1";
constexpr UINT AppBarCallback = WM_APP + 60;
constexpr UINT_PTR ClockTimer = 1, PlacementTimer = 2;
// The top edge comes first: it draws the status bar or the top bar. A display
// framed only for the top bar has just that edge.
constexpr UINT EdgeOrder[]{ABE_TOP, ABE_BOTTOM, ABE_LEFT, ABE_RIGHT};
constexpr int ButtonCount = static_cast<int>(BarButton::Count);
LRESULT CALLBACK EdgeProc(HWND, UINT, WPARAM, LPARAM);

// The desktop, the taskbar and the shell's own CoreWindows (Start, Search, the
// touch keyboard) can be foreground and display-sized without being full-screen apps.
bool IsShellSurface(HWND window) {
    wchar_t name[64]{};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    for (const auto shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Windows.UI.Core.CoreWindow", EdgeClass})
        if (wcscmp(name, shell) == 0) return true;
    return false;
}
bool RegisterEdgeClass() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = EdgeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = EdgeClass;
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
struct Wanted { std::wstring device; RECT monitor; UINT dpi; bool compact; };
std::vector<Wanted> AllDisplays() {
    std::vector<Wanted> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        UINT x = 0, y = 0;
        if (GetMonitorInfoW(monitor, &info) && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)))
            reinterpret_cast<std::vector<Wanted>*>(data)->push_back({info.szDevice, info.rcMonitor, x,
                IsCompactDisplay(info.rcMonitor.right - info.rcMonitor.left, x)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
// Every visible taskbar's rectangle. The bottom margin stacks on the taskbar,
// and not every resize sends ABN_POSCHANGED, so the refresh compares these.
std::vector<RECT> TaskbarRects() {
    std::vector<RECT> result;
    for (const auto name : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        HWND window = nullptr;
        while ((window = FindWindowExW(nullptr, window, name, nullptr)) != nullptr) {
            RECT bounds{};
            if (IsWindowVisible(window) && GetWindowRect(window, &bounds)) result.push_back(bounds);
        }
    }
    return result;
}
D2D1_COLOR_F Color(DWORD argb) {
    return D2D1::ColorF(((argb >> 16) & 255) / 255.f, ((argb >> 8) & 255) / 255.f, (argb & 255) / 255.f, (argb >> 24) / 255.f);
}
D2D1_RECT_F Rect(const RECT& rect) {
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}
// Touch and pen arrive as mouse messages tagged with this signature. They have
// no hover, so a tap must not leave the button highlighted.
bool FromTouch() { return (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700; }
// The notification-area artwork in the given ink, premultiplied for Direct2D.
// The artwork is monochrome, so its alpha is the whole flower.
ComPtr<ID2D1Bitmap> TintedIcon(ID2D1RenderTarget* target, int resource, int pixels, DWORD argb) {
    ComPtr<ID2D1Bitmap> bitmap;
    const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_ICON, pixels, pixels, 0));
    if (!icon) return bitmap;
    ICONINFO info{};
    const bool read = GetIconInfo(icon, &info) != FALSE;
    DestroyIcon(icon);
    if (!read) return bitmap;
    BITMAPINFO format{};
    format.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels, -pixels, 1, 32, BI_RGB};
    std::vector<BYTE> bits(static_cast<size_t>(pixels) * pixels * 4);
    const HDC dc = GetDC(nullptr);
    const bool copied = dc && info.hbmColor && GetDIBits(dc, info.hbmColor, 0, pixels, bits.data(), &format, DIB_RGB_COLORS) == pixels;
    if (dc) ReleaseDC(nullptr, dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    if (!copied) return bitmap;
    const UINT inkAlpha = argb >> 24, red = (argb >> 16) & 255, green = (argb >> 8) & 255, blue = argb & 255;
    for (size_t i = 0; i < bits.size(); i += 4) {
        const UINT alpha = (bits[i + 3] * inkAlpha + 127) / 255;
        bits[i] = static_cast<BYTE>((blue * alpha + 127) / 255);
        bits[i + 1] = static_cast<BYTE>((green * alpha + 127) / 255);
        bits[i + 2] = static_cast<BYTE>((red * alpha + 127) / 255);
        bits[i + 3] = static_cast<BYTE>(alpha);
    }
    target->CreateBitmap(D2D1::SizeU(pixels, pixels), bits.data(), static_cast<UINT32>(pixels) * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap);
    return bitmap;
}
// Where to draw a layout so that its ink, not its line box, is centered in box.
HRESULT InkOrigin(IDWriteTextLayout* layout, const RECT& box, D2D1_POINT_2F& origin) {
    DWRITE_OVERHANG_METRICS ink{};
    const HRESULT hr = layout->GetOverhangMetrics(&ink);
    if (FAILED(hr)) return hr;
    const float left = -ink.left, right = layout->GetMaxWidth() + ink.right;
    const float top = -ink.top, bottom = layout->GetMaxHeight() + ink.bottom;
    origin = {std::round((box.left + box.right) / 2.f - (left + right) / 2), std::round((box.top + box.bottom) / 2.f - (top + bottom) / 2)};
    return S_OK;
}
} // namespace

struct CompactScreen::Impl {
    enum class Bar { None, Status, Top };
    struct Edge {
        HWND window = nullptr;
        UINT side = ABE_TOP;
        bool registered = false;
        RECT requested{}, placed{};
    };
    struct Display {
        std::wstring device;
        RECT monitor{};
        UINT dpi = 96;
        bool compact = false; // All four edges, or only the top bar's.
        Edge edges[4];
        bool fullscreen = false, shown = false, open = false;
        BarButton hover = BarButton::None, pressed = BarButton::None;
        // What was drawn last, so identical repaints are skipped.
        Bar shownBar = Bar::None;
        bool shownLight = false;
        StatusButton shownStates[ButtonCount]{};
        std::wstring text;
        SIZE size{};
        RECT buttons[ButtonCount]{}; // Window coordinates; empty where the bar has no such button.
        Edge& Top() { return edges[0]; }
        const Edge& Top() const { return edges[0]; }
        size_t EdgeCount() const { return compact ? std::size(edges) : 1; }
        bool Registered() const {
            for (size_t i = 0; i < EdgeCount(); ++i) if (!edges[i].registered) return false;
            return true;
        }
        StatusButton State(int index) const {
            const auto button = static_cast<BarButton>(index);
            return pressed == button ? StatusButton::Pressed : open && button == BarButton::Menu ? StatusButton::Open
                : hover == button ? StatusButton::Hover : StatusButton::Rest;
        }
        BarButton At(POINT point) const {
            for (int i = 0; i < ButtonCount; ++i)
                if (!IsRectEmpty(&buttons[i]) && PtInRect(&buttons[i], point)) return static_cast<BarButton>(i);
            return BarButton::None;
        }
    };
    std::vector<std::unique_ptr<Display>> displays;
    HWND owner = nullptr;
    std::wstring clicked; // The display whose menu button was last clicked.
    std::vector<RECT> taskbars;
    ScreenOptions options;
    HRESULT result = S_OK;
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1Factory> draw;
    ComPtr<ID2D1DCRenderTarget> target;
    ComPtr<ID2D1StrokeStyle> round;
    std::wstring family, icons;

    ~Impl() { RemoveAll(); }
    Bar Content(const Display& display) const {
        if (options.topBar) return Bar::Top;
        return options.statusBar && display.compact ? Bar::Status : Bar::None;
    }
    Display* Find(HWND window) {
        for (auto& display : displays)
            for (auto& edge : display->edges) if (edge.window && edge.window == window) return display.get();
        return nullptr;
    }
    Display* FindDevice(const std::wstring& device) const {
        for (auto& display : displays) if (display->device == device) return display.get();
        return nullptr;
    }
    bool CreateEdges(Display& display) {
        if (!RegisterEdgeClass()) { result = HRESULT_FROM_WIN32(GetLastError()); return false; }
        for (size_t i = 0; i < display.EdgeCount(); ++i) {
            auto& edge = display.edges[i];
            edge.side = EdgeOrder[i];
            // Layered, never activated. The margins are also transparent to
            // input. The top edge takes clicks only where its pixels are not
            // fully transparent: its buttons. Elsewhere they fall through.
            const DWORD input = i == 0 ? 0 : WS_EX_TRANSPARENT;
            edge.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | input,
                EdgeClass, i == 0 ? L"jellyflower status bar" : L"jellyflower margin", WS_POPUP,
                display.monitor.left, display.monitor.top, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
            if (!edge.window) { result = HRESULT_FROM_WIN32(GetLastError()); return false; }
        }
        return true;
    }
    // Registers the edges as app bars and sizes them. ABM_SETPOS changes the
    // work area, which moves every maximized window, so it only runs when the
    // negotiated rectangle actually changes. That also keeps the app bars'
    // ABN_POSCHANGED notifications from bouncing between them.
    void Place(Display& display) {
        for (auto& edge : display.edges) {
            if (!edge.window) continue;
            APPBARDATA data{sizeof(data)};
            data.hWnd = edge.window;
            data.uCallbackMessage = AppBarCallback;
            if (!edge.registered) {
                if (!SHAppBarMessage(ABM_NEW, &data)) { result = HRESULT_FROM_WIN32(ERROR_NOT_READY); continue; }
                edge.registered = true;
                edge.requested = {};
            }
            const int thickness = edge.side == ABE_TOP ? TopEdgePixels(display.compact, display.dpi) : CompactEdgePixels(edge.side, display.dpi);
            data.uEdge = edge.side;
            data.rc = AppBarProposal(edge.side, display.monitor, thickness);
            SHAppBarMessage(ABM_QUERYPOS, &data);
            data.rc = AppBarPlacement(edge.side, data.rc, thickness);
            if (EqualRect(&data.rc, &edge.requested)) continue;
            edge.requested = data.rc;
            SHAppBarMessage(ABM_SETPOS, &data);
            edge.placed = data.rc;
            SetWindowPos(edge.window, HWND_TOPMOST, data.rc.left, data.rc.top, data.rc.right - data.rc.left,
                data.rc.bottom - data.rc.top, SWP_NOACTIVATE);
        }
    }
    void Release(Display& display) noexcept {
        KillTimer(display.Top().window, ClockTimer);
        KillTimer(display.Top().window, PlacementTimer);
        for (auto edge = std::rbegin(display.edges); edge != std::rend(display.edges); ++edge) {
            if (edge->window && edge->registered) {
                APPBARDATA data{sizeof(data)};
                data.hWnd = edge->window;
                SHAppBarMessage(ABM_REMOVE, &data);
            }
            edge->registered = false;
            if (edge->window) DestroyWindow(edge->window);
            edge->window = nullptr;
        }
    }
    void RemoveAll() noexcept {
        // Release can re-enter through sent app bar notifications; take the
        // list first so they find nothing.
        auto released = std::move(displays);
        displays.clear();
        for (auto& display : released) Release(*display);
    }
    void Refresh(const ScreenOptions& next) {
        const bool restyle = next.statusBar != options.statusBar || next.topBar != options.topBar
            || next.date != options.date || next.light != options.light;
        result = S_OK;
        options = next;
        auto wanted = AllDisplays();
        wanted.erase(std::remove_if(wanted.begin(), wanted.end(), [&](const Wanted& item) { return !item.compact && !options.topBar; }), wanted.end());
        const auto bars = TaskbarRects();
        const bool taskbarMoved = bars.size() != taskbars.size()
            || !std::equal(bars.begin(), bars.end(), taskbars.begin(), [](const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; });
        taskbars = bars;
        // Release displays that are no longer framed: unplugged, or no longer
        // compact with the top bar off. A display that became or stopped being
        // compact (rotated or rescaled) needs a different set of edges, so it
        // is released too and framed again below.
        for (size_t i = 0; i < displays.size();) {
            const auto& display = *displays[i];
            if (std::any_of(wanted.begin(), wanted.end(), [&](const Wanted& item) {
                    return item.device == display.device && item.compact == display.compact; })) { ++i; continue; }
            auto released = std::move(displays[i]);
            displays.erase(displays.begin() + static_cast<std::ptrdiff_t>(i));
            Release(*released);
        }
        for (const auto& item : wanted) {
            Display* display = FindDevice(item.device);
            if (!display) {
                auto created = std::make_unique<Display>();
                created->device = item.device;
                created->monitor = item.monitor;
                created->dpi = item.dpi;
                created->compact = item.compact;
                if (!CreateEdges(*created)) { Release(*created); continue; }
                display = created.get();
                displays.push_back(std::move(created));
                Place(*display);
                continue;
            }
            const bool moved = !EqualRect(&display->monitor, &item.monitor) || display->dpi != item.dpi;
            display->monitor = item.monitor;
            display->dpi = item.dpi;
            // Place only re-sets a position when the negotiated one differs.
            if (moved || taskbarMoved || !display->Registered()) Place(*display);
        }
        CheckFullscreen(false);
        for (auto& display : displays) Paint(*display, restyle);
    }
    void ShellRestarted() noexcept {
        for (auto& display : displays) {
            for (auto& edge : display->edges) { edge.registered = false; edge.requested = {}; }
        }
    }
    void CheckFullscreen(bool paint) {
        const HWND foreground = GetForegroundWindow();
        RECT bounds{};
        bool candidate = foreground && IsWindowVisible(foreground) && !IsIconic(foreground)
            && !IsShellSurface(foreground) && GetWindowRect(foreground, &bounds);
        DWORD cloaked = 0;
        if (candidate && SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) candidate = false;
        for (auto& display : displays) {
            const bool full = candidate && CoversMonitor(bounds, display->monitor);
            if (full == display->fullscreen) continue;
            display->fullscreen = full;
            if (paint) Paint(*display);
        }
    }
    void Notify(HWND window, WPARAM code) {
        auto* display = Find(window);
        if (!display) return;
        // Coalesce: the taskbar or another app bar moved, and each of our edges hears about it.
        if (code == ABN_POSCHANGED || code == ABN_STATECHANGE) SetTimer(display->Top().window, PlacementTimer, 100, nullptr);
        // Its lParam is unreliable (clicking the desktop can report a
        // full-screen app), so check the foreground window instead.
        else if (code == ABN_FULLSCREENAPP) CheckFullscreen(true);
    }
    void Timer(HWND window, UINT_PTR id) {
        auto* display = Find(window);
        if (!display) { KillTimer(window, id); return; }
        if (id == PlacementTimer) {
            KillTimer(window, id);
            Place(*display);
        }
        Paint(*display);
    }
    void Restyle(HWND window) {
        auto* display = Find(window);
        if (display && window == display->Top().window) Paint(*display, true);
    }
    LRESULT HitTest(HWND window, LPARAM lParam) {
        auto* display = Find(window);
        if (!display || window != display->Top().window || !display->shown) return HTTRANSPARENT;
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const POINT point{GET_X_LPARAM(lParam) - bounds.left, GET_Y_LPARAM(lParam) - bounds.top};
        return display->At(point) != BarButton::None ? HTCLIENT : HTTRANSPARENT;
    }
    void Mouse(HWND window, UINT message, LPARAM lParam) {
        auto* display = Find(window);
        if (!display || window != display->Top().window) return;
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const BarButton at = display->At(point);
        const bool touch = FromTouch();
        StatusButton before[ButtonCount]{};
        for (int i = 0; i < ButtonCount; ++i) before[i] = display->State(i);
        if (message == WM_MOUSEMOVE) {
            const BarButton hover = touch ? BarButton::None : at;
            if (display->hover == BarButton::None && hover != BarButton::None) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
            }
            display->hover = hover;
        } else if (message == WM_MOUSELEAVE) {
            display->hover = BarButton::None;
        } else if (message == WM_LBUTTONDOWN) {
            if (at != BarButton::None) { display->pressed = at; SetCapture(window); }
        } else if (message == WM_LBUTTONUP) {
            const BarButton click = display->pressed != BarButton::None && at == display->pressed ? at : BarButton::None;
            display->pressed = BarButton::None;
            if (touch) display->hover = BarButton::None;
            if (GetCapture() == window) ReleaseCapture();
            // Posted, not run here: the menu runs its own message loop, which
            // may refresh or release this window.
            if (click != BarButton::None && owner) {
                if (click == BarButton::Menu) clicked = display->device;
                PostMessageW(owner, BarButtonMessage, static_cast<WPARAM>(click), 0);
            }
        } else if (message == WM_CAPTURECHANGED) {
            display->pressed = BarButton::None;
        }
        for (int i = 0; i < ButtonCount; ++i)
            if (display->State(i) != before[i]) { Paint(*display); break; }
    }
    bool MenuButton(RECT& bounds, UINT& dpi) const {
        const auto* display = FindDevice(clicked);
        if (!display || !display->shown) return false;
        bounds = display->buttons[static_cast<int>(BarButton::Menu)];
        if (IsRectEmpty(&bounds)) return false;
        OffsetRect(&bounds, display->Top().placed.left, display->Top().placed.top);
        dpi = display->dpi;
        return true;
    }
    void MenuOpen(bool open) {
        auto* display = FindDevice(clicked);
        if (!display || display->open == open) return;
        display->open = open;
        Paint(*display);
    }
    bool Factories() {
        if (target) return true;
        if (!write && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write.GetAddressOf())))) return false;
        if (!draw && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, draw.GetAddressOf()))) return false;
        if (!round && FAILED(draw->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &round))) return false;
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(draw->CreateDCRenderTarget(&properties, &target))) return false;
        // Grayscale: ClearType assumes an opaque backdrop, and this one is the wallpaper.
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        ComPtr<IDWriteFontCollection> fonts;
        const auto installed = [&](const wchar_t* name) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            return fonts && SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists;
        };
        write->GetSystemFontCollection(&fonts);
        family = installed(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
        // Windows 10 has the glyph in the older icon font.
        icons = installed(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        return true;
    }
    HRESULT ClockFormat(float scale, ComPtr<IDWriteTextFormat>& format) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        HRESULT hr = write->CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, StatusBarTextSize * scale, locale, &format);
        if (SUCCEEDED(hr)) hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return hr;
    }
    // Centers the digits' ink, not the line box, so "am" and "pm" (with a
    // descender) sit on the same baseline every minute.
    HRESULT DigitsOffset(IDWriteTextFormat* format, int height, float& offset) {
        ComPtr<IDWriteTextLayout> digits;
        DWRITE_OVERHANG_METRICS ink{};
        HRESULT hr = write->CreateTextLayout(L"0123456789", 10, format, 1000, static_cast<float>(height), &digits);
        if (SUCCEEDED(hr)) hr = digits->GetOverhangMetrics(&ink);
        if (SUCCEEDED(hr)) offset = std::round(static_cast<float>(height) / 2 - (-ink.top + static_cast<float>(height) + ink.bottom) / 2);
        return hr;
    }
    void FillButton(const RECT& button, ID2D1Brush* brush, float scale) {
        const float radius = StatusButtonRadius * scale;
        target->FillRoundedRectangle(D2D1::RoundedRect(Rect(button), radius, radius), brush);
    }
    // Draws one frame of the top edge with no other fill, and hands the
    // premultiplied pixels to the layered window.
    bool Present(Display& display, const std::function<HRESULT()>& paint) {
        const HWND window = display.Top().window;
        const RECT bounds = display.Top().placed;
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        const HDC screen = GetDC(nullptr);
        const HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* bits = nullptr;
        const HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        HRESULT hr = E_OUTOFMEMORY;
        bool done = false;
        if (bitmap) {
            const auto previous = SelectObject(memory, bitmap);
            const RECT area{0, 0, width, height};
            if (SUCCEEDED(hr = target->BindDC(memory, &area))) {
                target->BeginDraw();
                target->Clear(D2D1::ColorF(0, 0));
                hr = paint();
                const HRESULT drawn = target->EndDraw();
                if (SUCCEEDED(hr)) hr = drawn;
                if (hr == D2DERR_RECREATE_TARGET) target.Reset();
            }
            if (SUCCEEDED(hr)) {
                POINT position{bounds.left, bounds.top}, origin{};
                SIZE size{width, height};
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                done = UpdateLayeredWindow(window, screen, &position, &size, memory, &origin, 0, &blend, ULW_ALPHA) != FALSE;
                if (!done) hr = HRESULT_FROM_WIN32(GetLastError());
            }
            SelectObject(memory, previous);
            DeleteObject(bitmap);
        }
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        if (!done) result = FAILED(hr) ? hr : E_FAIL;
        return done;
    }
    // The status bar: the time centered in the top margin, a chevron to its
    // right and the subtle fill behind both, one menu button.
    bool RenderStatus(Display& display, const std::wstring& text) {
        const RECT bounds = display.Top().placed;
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        const float scale = static_cast<float>(display.dpi) / 96;
        ComPtr<IDWriteTextFormat> format;
        ComPtr<IDWriteTextLayout> layout;
        float offset = 0;
        DWRITE_TEXT_METRICS metrics{};
        HRESULT hr = ClockFormat(scale, format);
        if (SUCCEEDED(hr)) hr = format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (SUCCEEDED(hr)) hr = write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
            static_cast<float>(width), static_cast<float>(height), &layout);
        if (SUCCEEDED(hr)) hr = DigitsOffset(format.Get(), height, offset);
        if (SUCCEEDED(hr)) hr = layout->GetMetrics(&metrics);
        // The chevron, as an open path pointing down, centered on the digits.
        ComPtr<ID2D1PathGeometry> chevron;
        ComPtr<ID2D1GeometrySink> sink;
        const float middle = std::round(static_cast<float>(height) / 2);
        const float chevronLeft = metrics.left + metrics.width + StatusChevronGap * scale;
        const float chevronRight = chevronLeft + StatusChevronWidth * scale, half = StatusChevronHeight * scale / 2;
        if (SUCCEEDED(hr)) hr = draw->CreatePathGeometry(&chevron);
        if (SUCCEEDED(hr)) hr = chevron->Open(&sink);
        if (SUCCEEDED(hr)) {
            sink->BeginFigure({chevronLeft, middle - half}, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine({(chevronLeft + chevronRight) / 2, middle + half});
            sink->AddLine({chevronRight, middle - half});
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            hr = sink->Close();
        }
        if (FAILED(hr)) { result = hr; return false; }
        const float padding = StatusButtonPadding * scale, inset = StatusButtonInset * scale;
        const RECT button{static_cast<LONG>(std::floor(metrics.left - padding)), static_cast<LONG>(std::round(inset)),
            static_cast<LONG>(std::ceil(chevronRight + padding)), static_cast<LONG>(height - std::round(inset))};
        const auto state = display.State(static_cast<int>(BarButton::Menu));
        const bool done = Present(display, [&]() -> HRESULT {
            ComPtr<ID2D1SolidColorBrush> fill, ink;
            HRESULT drawn = target->CreateSolidColorBrush(Color(StatusButtonFill(options.light, state)), &fill);
            if (SUCCEEDED(drawn)) drawn = target->CreateSolidColorBrush(Color(StatusButtonInk(options.light, state)), &ink);
            if (FAILED(drawn)) return drawn;
            FillButton(button, fill.Get(), scale);
            target->DrawTextLayout({0, offset}, layout.Get(), ink.Get());
            target->DrawGeometry(chevron.Get(), ink.Get(), StatusChevronStroke * scale, round.Get());
            return S_OK;
        });
        if (!done) return false;
        std::fill(std::begin(display.buttons), std::end(display.buttons), RECT{});
        display.buttons[static_cast<int>(BarButton::Menu)] = button;
        return true;
    }
    // The top bar (top_bar.hpp): the flower at the left, the Quick Settings
    // glyph and the clock at the right, each a subtle button like the status bar's.
    bool RenderTop(Display& display, const std::wstring& text) {
        const RECT bounds = display.Top().placed;
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        const float scale = static_cast<float>(display.dpi) / 96;
        ComPtr<IDWriteTextFormat> format, glyphFormat;
        ComPtr<IDWriteTextLayout> clock, glyph;
        float offset = 0;
        DWRITE_TEXT_METRICS metrics{};
        HRESULT hr = ClockFormat(scale, format);
        if (SUCCEEDED(hr)) hr = write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
            static_cast<float>(width), static_cast<float>(height), &clock);
        if (SUCCEEDED(hr)) hr = DigitsOffset(format.Get(), height, offset);
        if (SUCCEEDED(hr)) hr = clock->GetMetrics(&metrics);
        if (SUCCEEDED(hr)) hr = write->CreateTextFormat(icons.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, TopBarIconSize * scale, L"", &glyphFormat);
        if (SUCCEEDED(hr)) hr = write->CreateTextLayout(QuickSettingsGlyph, 1, glyphFormat.Get(), static_cast<float>(width),
            static_cast<float>(height), &glyph);
        const auto layout = LayoutTopBar(width, height, scale, metrics.width);
        const RECT& menu = layout.buttons[static_cast<int>(BarButton::Menu)];
        const RECT& quickSettings = layout.buttons[static_cast<int>(BarButton::QuickSettings)];
        const RECT& clockButton = layout.buttons[static_cast<int>(BarButton::Clock)];
        D2D1_POINT_2F glyphOrigin{};
        if (SUCCEEDED(hr)) hr = InkOrigin(glyph.Get(), quickSettings, glyphOrigin);
        if (FAILED(hr)) { result = hr; return false; }
        const int flower = static_cast<int>(std::lround(TopBarIconSize * scale));
        const D2D1_POINT_2F flowerOrigin{static_cast<float>(menu.left + (menu.right - menu.left - flower) / 2),
            static_cast<float>(menu.top + (menu.bottom - menu.top - flower) / 2)};
        const D2D1_POINT_2F clockOrigin{static_cast<float>(clockButton.left) + std::round(StatusButtonPadding * scale) - metrics.left, offset};
        const bool done = Present(display, [&]() -> HRESULT {
            for (int i = 0; i < ButtonCount; ++i) {
                const RECT& button = layout.buttons[i];
                if (IsRectEmpty(&button)) continue;
                const auto state = display.State(i);
                ComPtr<ID2D1SolidColorBrush> fill, ink;
                HRESULT drawn = target->CreateSolidColorBrush(Color(StatusButtonFill(options.light, state)), &fill);
                if (SUCCEEDED(drawn)) drawn = target->CreateSolidColorBrush(Color(StatusButtonInk(options.light, state)), &ink);
                if (FAILED(drawn)) return drawn;
                FillButton(button, fill.Get(), scale);
                const auto which = static_cast<BarButton>(i);
                if (which == BarButton::Menu) {
                    // Only the artwork's alpha is used, so either theme's variant will do.
                    const auto bitmap = TintedIcon(target.Get(), IDI_TRAY_LIGHT, flower, StatusButtonInk(options.light, state));
                    if (bitmap) target->DrawBitmap(bitmap.Get(), D2D1::RectF(flowerOrigin.x, flowerOrigin.y,
                        flowerOrigin.x + static_cast<float>(flower), flowerOrigin.y + static_cast<float>(flower)));
                } else if (which == BarButton::QuickSettings) {
                    target->DrawTextLayout(glyphOrigin, glyph.Get(), ink.Get());
                } else {
                    target->DrawTextLayout(clockOrigin, clock.Get(), ink.Get());
                }
            }
            return S_OK;
        });
        if (!done) return false;
        std::copy(std::begin(layout.buttons), std::end(layout.buttons), std::begin(display.buttons));
        return true;
    }
    void Paint(Display& display, bool force = false) {
        auto& top = display.Top();
        if (!top.window) return;
        const Bar bar = Content(display);
        if (bar == Bar::None || display.fullscreen || !top.registered) {
            KillTimer(top.window, ClockTimer);
            if (display.shown) ShowWindow(top.window, SW_HIDE);
            display.shown = false;
            display.hover = display.pressed = BarButton::None;
            std::fill(std::begin(display.buttons), std::end(display.buttons), RECT{});
            display.shownBar = Bar::None;
            display.text.clear();
            return;
        }
        SYSTEMTIME now{};
        GetLocalTime(&now);
        SetTimer(top.window, ClockTimer, NextMinuteDelay(now), nullptr);
        // The status bar shows the time alone. The top bar follows the date
        // setting, as the taskbar's clock does.
        const auto text = LocalizedClock(now, bar == Bar::Top && options.date);
        StatusButton states[ButtonCount]{};
        for (int i = 0; i < ButtonCount; ++i) states[i] = display.State(i);
        const SIZE size{top.placed.right - top.placed.left, top.placed.bottom - top.placed.top};
        if (text.empty() || size.cx <= 0 || size.cy <= 0) return;
        if (!force && display.shown && bar == display.shownBar && text == display.text && display.shownLight == options.light
            && std::equal(std::begin(states), std::end(states), std::begin(display.shownStates))
            && size.cx == display.size.cx && size.cy == display.size.cy) return;
        if (!Factories()) { result = E_FAIL; return; }
        if (!(bar == Bar::Top ? RenderTop(display, text) : RenderStatus(display, text))) return;
        display.shownBar = bar;
        display.text = text;
        display.shownLight = options.light;
        std::copy(std::begin(states), std::end(states), std::begin(display.shownStates));
        display.size = size;
        if (!display.shown) ShowWindow(top.window, SW_SHOWNOACTIVATE);
        display.shown = true;
    }
};

namespace {
LRESULT CALLBACK EdgeProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CompactScreen::Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CompactScreen::Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == AppBarCallback) {
        if (self) self->Notify(window, wParam);
        return 0;
    }
    switch (message) {
    case WM_NCHITTEST: return self ? self->HitTest(window, lParam) : HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_CAPTURECHANGED:
        if (self) self->Mouse(window, message, lParam);
        return 0;
    case WM_DPICHANGED: return 0; // The app bar negotiation owns the size.
    case WM_TIMER: if (self) self->Timer(window, wParam); return 0;
    case WM_TIMECHANGE: case WM_SETTINGCHANGE: if (self) self->Restyle(window); return 0;
    case WM_NCDESTROY: SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

CompactScreen::CompactScreen() : impl_(std::make_unique<Impl>()) {}
CompactScreen::~CompactScreen() = default;
void CompactScreen::Owner(HWND owner) noexcept { impl_->owner = owner; }
void CompactScreen::Refresh(const ScreenOptions& options) { impl_->Refresh(options); }
void CompactScreen::ShellRestarted() noexcept { impl_->ShellRestarted(); }
void CompactScreen::ForegroundChanged() { impl_->CheckFullscreen(true); }
void CompactScreen::Remove() noexcept { impl_->RemoveAll(); }
LONG CompactScreen::Displays() const noexcept {
    return static_cast<LONG>(std::count_if(impl_->displays.begin(), impl_->displays.end(),
        [](const auto& display) { return display->compact && display->Registered(); }));
}
LONG CompactScreen::TopBars() const noexcept {
    if (!impl_->options.topBar) return 0;
    return static_cast<LONG>(std::count_if(impl_->displays.begin(), impl_->displays.end(),
        [](const auto& display) { return display->Registered(); }));
}
HRESULT CompactScreen::Result() const noexcept { return impl_->result; }
bool CompactScreen::MenuButton(RECT& bounds, UINT& dpi) const { return impl_->MenuButton(bounds, dpi); }
void CompactScreen::MenuOpen(bool open) { impl_->MenuOpen(open); }
} // namespace acrylic

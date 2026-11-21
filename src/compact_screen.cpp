// SPDX-License-Identifier: GPL-3.0-or-later
#include "compact_screen.hpp"
#include "compact.hpp"
#include "clock_text.hpp"
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
#include <iterator>
#include <string>
#include <vector>

namespace acrylic {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t EdgeClass[] = L"AcrylicTaskbar.ScreenEdge.v1";
constexpr UINT AppBarCallback = WM_APP + 60;
constexpr UINT_PTR ClockTimer = 1, PlacementTimer = 2;
// The top edge comes first: it is the status bar.
constexpr UINT EdgeOrder[]{ABE_TOP, ABE_BOTTOM, ABE_LEFT, ABE_RIGHT};
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
struct Wanted { std::wstring device; RECT monitor; UINT dpi; };
std::vector<Wanted> CompactDisplays() {
    std::vector<Wanted> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        UINT x = 0, y = 0;
        if (GetMonitorInfoW(monitor, &info) && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y))
            && IsCompactDisplay(info.rcMonitor.right - info.rcMonitor.left, x))
            reinterpret_cast<std::vector<Wanted>*>(data)->push_back({info.szDevice, info.rcMonitor, x});
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
// Touch and pen arrive as mouse messages tagged with this signature. They have
// no hover, so a tap must not leave the button highlighted.
bool FromTouch() { return (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700; }
} // namespace

struct CompactScreen::Impl {
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
        Edge edges[4];
        bool fullscreen = false, shown = false, shownLight = false;
        bool hover = false, pressed = false, open = false;
        StatusButton shownState = StatusButton::Rest;
        std::wstring text;
        SIZE size{};
        RECT button{}; // Window coordinates of the time and chevron.
        Edge& Top() { return edges[0]; }
        const Edge& Top() const { return edges[0]; }
        bool Registered() const { return std::all_of(std::begin(edges), std::end(edges), [](const Edge& edge) { return edge.registered; }); }
        StatusButton State() const {
            return pressed ? StatusButton::Pressed : open ? StatusButton::Open : hover ? StatusButton::Hover : StatusButton::Rest;
        }
    };
    std::vector<std::unique_ptr<Display>> displays;
    HWND owner = nullptr;
    std::wstring clicked; // The display whose menu button was last clicked.
    std::vector<RECT> taskbars;
    bool statusBar = true, light = false;
    HRESULT result = S_OK;
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1Factory> draw;
    ComPtr<ID2D1DCRenderTarget> target;
    ComPtr<ID2D1StrokeStyle> round;
    std::wstring family;

    ~Impl() { RemoveAll(); }
    Display* Find(HWND window) {
        for (auto& display : displays)
            for (auto& edge : display->edges) if (edge.window == window) return display.get();
        return nullptr;
    }
    Display* FindDevice(const std::wstring& device) const {
        for (auto& display : displays) if (display->device == device) return display.get();
        return nullptr;
    }
    bool CreateEdges(Display& display) {
        if (!RegisterEdgeClass()) { result = HRESULT_FROM_WIN32(GetLastError()); return false; }
        for (size_t i = 0; i < std::size(display.edges); ++i) {
            auto& edge = display.edges[i];
            edge.side = EdgeOrder[i];
            // Layered, never activated. The margins are also transparent to
            // input. The status bar takes clicks only where its pixels are not
            // fully transparent: the menu button. Elsewhere they fall through.
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
            const int thickness = CompactEdgePixels(edge.side, display.dpi);
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
    void Refresh(bool bar, bool lightInk) {
        const bool restyle = bar != statusBar || lightInk != light;
        result = S_OK;
        statusBar = bar;
        light = lightInk;
        const auto wanted = CompactDisplays();
        const auto bars = TaskbarRects();
        const bool taskbarMoved = bars.size() != taskbars.size()
            || !std::equal(bars.begin(), bars.end(), taskbars.begin(), [](const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; });
        taskbars = bars;
        // Release displays that are no longer compact: rotated, rescaled or unplugged.
        for (size_t i = 0; i < displays.size();) {
            const auto& device = displays[i]->device;
            if (std::any_of(wanted.begin(), wanted.end(), [&](const Wanted& item) { return item.device == device; })) { ++i; continue; }
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
        return PtInRect(&display->button, point) ? HTCLIENT : HTTRANSPARENT;
    }
    void Mouse(HWND window, UINT message, LPARAM lParam) {
        auto* display = Find(window);
        if (!display || window != display->Top().window) return;
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const bool inside = PtInRect(&display->button, point) != FALSE, touch = FromTouch();
        const auto before = display->State();
        if (message == WM_MOUSEMOVE) {
            if (!display->hover && inside && !touch) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
            }
            display->hover = inside && !touch;
        } else if (message == WM_MOUSELEAVE) {
            display->hover = false;
        } else if (message == WM_LBUTTONDOWN) {
            if (inside) { display->pressed = true; SetCapture(window); }
        } else if (message == WM_LBUTTONUP) {
            const bool click = display->pressed && inside;
            display->pressed = false;
            if (touch) display->hover = false;
            if (GetCapture() == window) ReleaseCapture();
            // Posted, not run here: the menu runs its own message loop, which
            // may refresh or release this window.
            if (click && owner) { clicked = display->device; PostMessageW(owner, StatusBarMenuMessage, 0, 0); }
        } else if (message == WM_CAPTURECHANGED) {
            display->pressed = false;
        }
        if (display->State() != before) Paint(*display);
    }
    bool MenuButton(RECT& bounds, UINT& dpi) const {
        const auto* display = FindDevice(clicked);
        if (!display || !display->shown || IsRectEmpty(&display->button)) return false;
        bounds = display->button;
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
        family = L"Segoe UI";
        ComPtr<IDWriteFontCollection> fonts;
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(write->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
            family = L"Segoe UI Variable Text";
        return true;
    }
    // Draws the menu button (the time centered in the top margin, a chevron to
    // its right and the subtle fill behind both) with no other fill, and hands
    // the premultiplied pixels to the layered window.
    bool Render(Display& display, const std::wstring& text, StatusButton state) {
        if (!Factories()) { result = E_FAIL; return false; }
        const HWND window = display.Top().window;
        const RECT bounds = display.Top().placed;
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        const float scale = static_cast<float>(display.dpi) / 96;
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        ComPtr<IDWriteTextFormat> format;
        ComPtr<IDWriteTextLayout> layout, digits;
        HRESULT hr = write->CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, StatusBarTextSize * scale, locale, &format);
        if (SUCCEEDED(hr)) hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (SUCCEEDED(hr)) hr = format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (SUCCEEDED(hr)) hr = write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
            static_cast<float>(width), static_cast<float>(height), &layout);
        // Center the digits' ink, not the line box, so "am" and "pm" (with a
        // descender) sit on the same baseline every minute.
        if (SUCCEEDED(hr)) hr = write->CreateTextLayout(L"0123456789", 10, format.Get(), static_cast<float>(width), static_cast<float>(height), &digits);
        DWRITE_OVERHANG_METRICS ink{};
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(hr)) hr = digits->GetOverhangMetrics(&ink);
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
        const float inkTop = -ink.top, inkBottom = static_cast<float>(height) + ink.bottom;
        const float offset = std::round(static_cast<float>(height) / 2 - (inkTop + inkBottom) / 2);
        const float padding = StatusButtonPadding * scale, inset = StatusButtonInset * scale;
        const RECT button{static_cast<LONG>(std::floor(metrics.left - padding)), static_cast<LONG>(std::round(inset)),
            static_cast<LONG>(std::ceil(chevronRight + padding)), static_cast<LONG>(height - std::round(inset))};

        const HDC screen = GetDC(nullptr);
        const HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* bits = nullptr;
        const HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        bool done = false;
        if (bitmap) {
            const auto previous = SelectObject(memory, bitmap);
            const RECT area{0, 0, width, height};
            ComPtr<ID2D1SolidColorBrush> fill, brush;
            if (SUCCEEDED(hr = target->BindDC(memory, &area))) {
                target->BeginDraw();
                target->Clear(D2D1::ColorF(0, 0));
                hr = target->CreateSolidColorBrush(Color(StatusButtonFill(light, state)), &fill);
                if (SUCCEEDED(hr)) hr = target->CreateSolidColorBrush(Color(StatusButtonInk(light, state)), &brush);
                if (SUCCEEDED(hr)) {
                    const float radius = StatusButtonRadius * scale;
                    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(static_cast<float>(button.left), static_cast<float>(button.top),
                        static_cast<float>(button.right), static_cast<float>(button.bottom)), radius, radius), fill.Get());
                    target->DrawTextLayout({0, offset}, layout.Get(), brush.Get());
                    target->DrawGeometry(chevron.Get(), brush.Get(), StatusChevronStroke * scale, round.Get());
                }
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
        } else hr = E_OUTOFMEMORY;
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        if (done) display.button = button;
        else result = FAILED(hr) ? hr : E_FAIL;
        return done;
    }
    void Paint(Display& display, bool force = false) {
        auto& top = display.Top();
        if (!top.window) return;
        if (!statusBar || display.fullscreen || !top.registered) {
            KillTimer(top.window, ClockTimer);
            if (display.shown) ShowWindow(top.window, SW_HIDE);
            display.shown = false;
            display.hover = display.pressed = false;
            display.button = {};
            display.text.clear();
            return;
        }
        SYSTEMTIME now{};
        GetLocalTime(&now);
        SetTimer(top.window, ClockTimer, NextMinuteDelay(now), nullptr);
        const auto text = LocalizedClock(now, false);
        const auto state = display.State();
        const SIZE size{top.placed.right - top.placed.left, top.placed.bottom - top.placed.top};
        if (text.empty() || size.cx <= 0 || size.cy <= 0) return;
        if (!force && display.shown && text == display.text && display.shownLight == light && state == display.shownState
            && size.cx == display.size.cx && size.cy == display.size.cy) return;
        if (!Render(display, text, state)) return;
        display.text = text;
        display.shownLight = light;
        display.shownState = state;
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
void CompactScreen::Refresh(bool statusBar, bool light) { impl_->Refresh(statusBar, light); }
void CompactScreen::ShellRestarted() noexcept { impl_->ShellRestarted(); }
void CompactScreen::ForegroundChanged() { impl_->CheckFullscreen(true); }
void CompactScreen::Remove() noexcept { impl_->RemoveAll(); }
LONG CompactScreen::Displays() const noexcept {
    return static_cast<LONG>(std::count_if(impl_->displays.begin(), impl_->displays.end(),
        [](const auto& display) { return display->Registered(); }));
}
HRESULT CompactScreen::Result() const noexcept { return impl_->result; }
bool CompactScreen::MenuButton(RECT& bounds, UINT& dpi) const { return impl_->MenuButton(bounds, dpi); }
void CompactScreen::MenuOpen(bool open) { impl_->MenuOpen(open); }
} // namespace acrylic

// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar_glass.hpp"
#include "fluted_glass.hpp"
#include "glass_compositor.hpp"
#include "glass_state.hpp"
#include <dwmapi.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>
#include <array>

namespace acrylic {
namespace {
namespace composition = winrt::Windows::UI::Composition;

constexpr auto UseHostBackdrop = static_cast<DWMWINDOWATTRIBUTE>(17);
constexpr DWORD SquareCorners = 1; // DWMWCP_DONOTROUND: the glass rounds its own corners.
constexpr UINT SnapDelays[]{150, 450, 1000}; // Milliseconds after a change, then after each snapshot.

LRESULT CALLBACK GlassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}
bool Registered() noexcept {
    static const bool registered = [] {
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = GlassProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = TaskbarGlass::WindowClass;
        return RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}
// Fluted glass takes system acrylic's legacy tint (ABGR) over its reeds.
winrt::Windows::UI::Color GlassTint(bool light) noexcept {
    const DWORD c = Recipe(AcrylicMode::Fluted, light).legacyColor;
    return {static_cast<uint8_t>(c >> 24), static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16)};
}

// One glass window.
struct Pane {
    HWND window = nullptr;
    GlassSlot slot{};
    composition::Desktop::DesktopWindowTarget target{nullptr};
    std::unique_ptr<FlutedGlass> glass;
    RECT placed{};

    void Open(winrt::Windows::UI::Color tint) {
        if (!Registered()) winrt::throw_last_error();
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, TaskbarGlass::WindowClass, L"",
            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) winrt::throw_last_error();
        const BOOL on = TRUE;
        winrt::check_hresult(DwmSetWindowAttribute(window, UseHostBackdrop, &on, sizeof(on)));
        DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &on, sizeof(on));
        DwmSetWindowAttribute(window, DWMWA_EXCLUDED_FROM_PEEK, &on, sizeof(on));
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &SquareCorners, sizeof(SquareCorners));
        const MARGINS whole{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(window, &whole);
        const auto compositor = GlassCompositor();
        namespace abi = ABI::Windows::UI::Composition::Desktop;
        winrt::check_hresult(compositor.as<abi::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(window, FALSE,
            reinterpret_cast<abi::IDesktopWindowTarget**>(winrt::put_abi(target))));
        // The host backdrop is what's behind the glass's window, blurred by Windows.
        glass = std::make_unique<FlutedGlass>(compositor, GlassReeds(), compositor.CreateHostBackdropBrush(), tint);
        target.Root(glass->Root());
    }
    // Windows keeps a host backdrop live only for the active window, and the glass never is, so its
    // backdrop is a snapshot; a new brush takes a new one.
    void Resnap() {
        if (glass) glass->Page(GlassCompositor().CreateHostBackdropBrush());
    }
    void Close() noexcept {
        target = nullptr;
        glass.reset();
        if (window) DestroyWindow(window);
        window = nullptr;
        slot = {};
        placed = {};
    }
    void Hide() noexcept {
        if (window && IsWindowVisible(window)) ShowWindow(window, SW_HIDE);
        placed = {};
    }
    // Over the background's place, directly below its taskbar window, or hidden while the taskbar is.
    bool Place() {
        const HWND bar = slot.taskbar;
        RECT frame{};
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(bar, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (!window || !IsWindowVisible(bar) || cloaked || !GetWindowRect(bar, &frame)) { Hide(); return false; }
        const RECT rect{frame.left + slot.left, frame.top + slot.top, frame.left + slot.left + slot.width, frame.top + slot.top + slot.height};
        const UINT dpi = GetDpiForWindow(bar);
        glass->Layout({static_cast<float>(slot.width), static_cast<float>(slot.height)}, dpi ? dpi : 96, static_cast<float>(slot.corner));
        if (!EqualRect(&rect, &placed) || GetWindow(bar, GW_HWNDNEXT) != window || !IsWindowVisible(window)) {
            if (!SetWindowPos(window, bar, rect.left, rect.top, slot.width, slot.height, SWP_NOACTIVATE | SWP_SHOWWINDOW)) winrt::throw_last_error();
            placed = rect;
            Resnap();
        }
        return true;
    }
};
}

struct TaskbarGlass::Impl {
    std::array<Pane, GlassSlotCount> panes;
    HWINEVENTHOOK moved = nullptr, foreground = nullptr, changed = nullptr, appeared = nullptr;
    DWORD explorer = 0;
    UINT_PTR settle = 0, snap = 0;
    int snaps = 0;
    HRESULT error = S_OK;
    SharedState* state = nullptr;

    // The controller keeps one; Windows' event callbacks find it here.
    static Impl*& Active() noexcept {
        static Impl* active = nullptr;
        return active;
    }
    // Follows the taskbar windows while any glass is open: their moves on the same turn of the loop, and
    // the z-order again after the foreground changes, when Explorer may move the taskbar below a
    // full-screen app. Windows moving, opening, closing or minimizing may change what's behind the
    // glass, so each takes a new snapshot shortly after.
    void Watch(DWORD explorerPid, bool open) noexcept {
        if (!open || explorer != explorerPid) Unwatch();
        if (!open) return;
        explorer = explorerPid;
        Active() = this;
        if (!moved) moved = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, Moved, explorerPid, 0, WINEVENT_OUTOFCONTEXT);
        if (!foreground) foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, Foreground, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!changed) changed = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MINIMIZEEND, nullptr, Changed, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!appeared) appeared = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, Changed, 0, 0, WINEVENT_OUTOFCONTEXT);
    }
    void Unwatch() noexcept {
        for (const auto hook : {moved, foreground, changed, appeared}) if (hook) UnhookWinEvent(hook);
        for (const auto timer : {settle, snap}) if (timer) KillTimer(nullptr, timer);
        moved = foreground = changed = appeared = nullptr;
        settle = snap = 0;
        explorer = 0;
        if (Active() == this) Active() = nullptr;
    }
    void Place(Pane& pane) noexcept {
        try { pane.Place(); } catch (...) { error = winrt::to_hresult(); }
    }
    static void CALLBACK Moved(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG, DWORD, DWORD) {
        if (object != OBJID_WINDOW || !Active()) return;
        for (auto& pane : Active()->panes) if (pane.window && pane.slot.taskbar == window) Active()->Place(pane);
    }
    static void CALLBACK Foreground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        auto* self = Active();
        if (!self) return;
        for (auto& pane : self->panes) if (pane.window) self->Place(pane);
        if (self->settle) KillTimer(nullptr, self->settle);
        self->settle = SetTimer(nullptr, 0, 300, Settled);
        self->Soon();
    }
    static void CALLBACK Changed(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
        auto* self = Active();
        if (!self || object != OBJID_WINDOW || child != CHILDID_SELF) return;
        for (const auto& pane : self->panes) if (window == pane.window) return;
        self->Soon();
    }
    // Windows refreshes the snapshot it hands a new brush on its own schedule, so a burst of changes
    // takes three: soon, once an opening or closing animation is over, and once more after that.
    void Soon() noexcept {
        snaps = 0;
        if (!snap) snap = SetTimer(nullptr, 0, SnapDelays[0], Snapped);
    }
    void Resnap() noexcept {
        for (auto& pane : panes) {
            try { pane.Resnap(); } catch (...) { error = winrt::to_hresult(); }
        }
    }
    static void CALLBACK Snapped(HWND, UINT, UINT_PTR timer, DWORD) {
        KillTimer(nullptr, timer);
        auto* self = Active();
        if (!self) return;
        self->snap = ++self->snaps < static_cast<int>(std::size(SnapDelays)) ? SetTimer(nullptr, 0, SnapDelays[self->snaps], Snapped) : 0;
        self->Resnap();
    }
    static void CALLBACK Settled(HWND, UINT, UINT_PTR timer, DWORD) {
        KillTimer(nullptr, timer);
        auto* self = Active();
        if (!self) return;
        self->settle = 0;
        for (auto& pane : self->panes) if (pane.window) self->Place(pane);
    }
};

TaskbarGlass::TaskbarGlass() : impl(std::make_unique<Impl>()) {}
// The shared state may be unmapped by now; the windows go with the controller anyway.
TaskbarGlass::~TaskbarGlass() { Remove(nullptr); }

bool TaskbarGlass::Refresh(SharedState* state, DWORD explorerPid, bool light) noexcept {
    auto& i = *impl;
    if (!state) { Remove(i.state); return false; }
    i.state = state;
    std::array<GlassSlot, GlassSlotCount> slots{};
    if (!ReadGlassSlots(*state, slots)) return false; // The helper is writing; its message follows.
    LONG shown = 0;
    bool open = false;
    i.error = S_OK;
    for (size_t n = 0; n < GlassSlotCount; ++n) {
        auto& pane = i.panes[n];
        const auto& slot = slots[n];
        if (slot.Empty() || !IsWindow(slot.taskbar)) { pane.Close(); continue; }
        try {
            if (pane.window && pane.slot.taskbar != slot.taskbar) pane.Close();
            if (!pane.window) pane.Open(GlassTint(light));
            else pane.glass->Tint(GlassTint(light));
            pane.slot = slot;
            if (pane.Place()) shown |= 1 << n;
            open = true;
        } catch (...) {
            i.error = winrt::to_hresult();
            pane.Close();
        }
    }
    i.Watch(explorerPid, open);
    i.Resnap();
    InterlockedExchange(&state->glassResult, i.error);
    return InterlockedExchange(&state->glassShown, shown) != shown;
}

void TaskbarGlass::Remove(SharedState* state) noexcept {
    impl->Unwatch();
    for (auto& pane : impl->panes) pane.Close();
    if (state) InterlockedExchange(&state->glassShown, 0);
    impl->state = nullptr;
}

HRESULT TaskbarGlass::Error() const noexcept { return impl->error; }
}

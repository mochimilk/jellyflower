// SPDX-License-Identifier: GPL-3.0-or-later
// Taskbar XAML identification and Backdrop acrylic approach are adapted from
// TranslucentTB's ExplorerTAP. See THIRD_PARTY_NOTICES.md for pinned sources.
#include "shared.hpp"
#include "bridge_notify.hpp"
#include "theme.hpp"
#include "clock.hpp"
#include "adaptive_session.hpp"
#include "volume.hpp"
#include "chevron.hpp"
#include "tray_only.hpp"
#include "dock.hpp"
#include "bar_height.hpp"
#include "acrylic_material.hpp"
#include "glass_state.hpp"
#include <unknwn.h>
#include <shobjidl.h>
#include <xamlOM.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace acrylic;
namespace xaml = winrt::Windows::UI::Xaml;
namespace media = xaml::Media;
namespace shapes = xaml::Shapes;

namespace {
Handle mapping;
SharedState* state = nullptr;
std::atomic_bool booting{false};
std::atomic_uint endpointSerial{0};
HMODULE module = nullptr;
void Result(HRESULT hr) noexcept {
    if (state) InterlockedExchange(&state->lastResult, hr);
}

// The process handle distinguishes a dead controller from a reused PID. The
// epoch forces opening a new handle when another controller attaches later.
struct Owner {
    LONG epoch = -1;
    Handle process;
    bool Alive() noexcept {
        if (!state || Read(state->magic) != Magic || !Read(state->running)) return false;
        const LONG currentEpoch = Read(state->epoch);
        if (currentEpoch != epoch || !process) {
            epoch = currentEpoch;
            process.reset(OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(Read(state->controllerPid))));
        }
        return process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
    }
};
thread_local Owner owner;
bool AdaptiveOwnerAlive(){return owner.Alive();}
class Background;
auto& Backgrounds() {
    // Explorer-lifetime storage avoids calling XAML during TLS/loader teardown.
    // Removed elements are released normally on their own UI thread.
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Background>>;
    return *records;
}
void CALLBACK BackgroundTimer(HWND, UINT, UINT_PTR timer, DWORD);
void RefreshBackgrounds() noexcept;
void RefreshClocks() noexcept;
void RefreshVolumes() noexcept;
void RefreshChevrons() noexcept;
void RefreshTrays() noexcept;
void RefreshDocks() noexcept;
void SoonDocks() noexcept;

// Slots in glass_state.hpp, shared by every taskbar thread in Explorer.
std::atomic<unsigned> glassSlots{0};
int TakeGlassSlot() noexcept {
    for (unsigned slot = 0; slot < GlassSlotCount; ++slot) {
        const unsigned bit = 1u << slot;
        if (!(glassSlots.fetch_or(bit) & bit)) return static_cast<int>(slot);
    }
    return -1;
}
void GiveGlassSlot(int slot) noexcept { glassSlots.fetch_and(~(1u << slot)); }
// The controller draws the glass; tell it where it goes now.
void NotifyGlass() noexcept {
    const HWND controller = FindWindowW(WindowClass, nullptr);
    DWORD pid = 0;
    if (controller) GetWindowThreadProcessId(controller, &pid);
    if (state && pid && pid == static_cast<DWORD>(Read(state->controllerPid))) PostMessageW(controller, GlassMessage, 0, 0);
}

class Background : public std::enable_shared_from_this<Background> {
public:
    shapes::Rectangle shape;
    media::Brush original{nullptr};
    media::AcrylicBrush acrylic{nullptr};
    media::Brush clear{nullptr}; // The border's fill, and the background's under fluted glass.
    media::Brush effect{nullptr};
    GlassSlot published{};
    int slot = -1;
    winrt::event_token layout{};
    bool border = false, light = false, fluted = false, listening = false;
    AcrylicMode mode = AcrylicMode::Off;
    UINT_PTR timer = 0;
    int64_t changedToken = 0;
    bool registered = false, updating = false, counted = false;

    explicit Background(shapes::Rectangle rectangle, bool isBorder) : shape(std::move(rectangle)), original(shape.Fill()), border(isBorder) {
        clear = media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0});
        if (border) {
            effect = clear;
            return;
        }
        acrylic = media::AcrylicBrush();
        // The transparent taskbar hosts the XAML backdrop. HostBackdrop loses
        // acrylic on deactivation; Backdrop follows TranslucentTB's approach.
        acrylic.BackgroundSource(media::AcrylicBackgroundSource::Backdrop);
        acrylic.AlwaysUseFallback(false); // This does NOT override system fallback policy.
        UpdateTheme();
        effect = acrylic;
    }
    void UpdateTheme() {
        light = SystemLightTheme();
        mode = state ? DecodeAcrylicMode(Read(state->active)) : AcrylicMode::System;
        ConfigureAcrylic(acrylic, mode, light);
    }
    void Start() {
        const std::weak_ptr<Background> weak = shared_from_this();
        changedToken = shape.RegisterPropertyChangedCallback(shapes::Shape::FillProperty(),
            [weak](const xaml::DependencyObject&, const xaml::DependencyProperty&) {
                if (const auto self = weak.lock()) self->OnChanged();
            });
        registered = true;
        if (!EnsureBridgeNotifyWindow(module, RefreshBackgrounds)) {
            const DWORD error = GetLastError();
            Result(HRESULT_FROM_WIN32(error ? error : ERROR_NOT_ENOUGH_MEMORY));
            // Keep the recovery timer usable even if the fast receiver fails.
        }
        timer = SetTimer(nullptr, 0, 2000, BackgroundTimer);
        if (!timer) winrt::throw_last_error();
        Refresh();
    }
    void OnChanged() noexcept {
        if (updating) return;
        try {
            const auto current = shape.Fill();
            if (current != effect) {
                // Keep the most recent stock brush, including theme changes.
                original = current;
                Refresh();
            }
        } catch (...) { Result(winrt::to_hresult()); }
    }
    void Refresh() noexcept {
        try {
            // The dock reuses the stroke rectangle as its outline, so its fill
            // stays clear while the dock is on, even with acrylic off.
            if (owner.Alive() && (Read(state->active) != 0 || (border && DockOn(*state)))) {
                if (!border && (light != SystemLightTheme() || mode != DecodeAcrylicMode(Read(state->active)))) UpdateTheme();
                const bool flutes = !border && Flute();
                if (shape.Fill() != effect) {
                    updating = true;
                    shape.Fill(effect);
                    updating = false;
                }
                // The fill is back before the glass goes, so the pill never shows bare.
                if (!border && !flutes) Release();
                if (!counted) {
                    counted = true;
                    InterlockedIncrement(border ? &state->borderCount : &state->appliedCount);
                }
            } else Restore();
        } catch (...) {
            updating = false;
            Result(winrt::to_hresult());
        }
    }
    // Fluted glass where Windows allows its materials, acrylic otherwise, which
    // follows Windows' own fallbacks. The controller draws the glass behind the
    // taskbar (glass_state.hpp); the fill stays acrylic until it says the glass
    // is up, then clears so the glass shows through. True while fluted is wanted.
    bool Flute() noexcept {
        const bool wanted = mode == AcrylicMode::Fluted && FlutedGlassAllowed();
        if (wanted) InterlockedExchange(&state->flutedResult, Publish());
        const bool on = wanted && slot >= 0 && GlassShown(*state, static_cast<size_t>(slot));
        Count(on);
        effect = on ? clear : acrylic;
        return wanted;
    }
    // The background's place relative to its taskbar window, followed through
    // layout passes such as the dock's pill growing and shrinking.
    HRESULT Publish() noexcept {
        try {
            if (slot < 0 && (slot = TakeGlassSlot()) < 0) return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
            if (!listening) {
                const std::weak_ptr<Background> weak = shared_from_this();
                layout = shape.LayoutUpdated([weak](const auto&, const auto&) { if (const auto self = weak.lock()) self->Publish(); });
                listening = true;
            }
            RECT bounds{};
            double scale = 1;
            HRESULT error = S_OK;
            GlassSlot next{};
            if (const HWND bar = TaskbarBounds(shape, bounds, scale, error))
                next = {bar, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, std::lround(shape.RadiusX() * scale)};
            if (next != published) {
                WriteGlassSlot(*state, static_cast<size_t>(slot), next);
                published = next;
                NotifyGlass();
            }
            return error;
        } catch (...) {
            return winrt::to_hresult();
        }
    }
    void Release() noexcept {
        if (listening) {
            listening = false;
            try { shape.LayoutUpdated(layout); } catch (...) {}
        }
        if (slot < 0) return;
        if (state) WriteGlassSlot(*state, static_cast<size_t>(slot), {});
        GiveGlassSlot(slot);
        slot = -1;
        published = {};
        NotifyGlass();
    }
    void Count(bool on) noexcept {
        if (on == fluted) return;
        fluted = on;
        if (on) InterlockedIncrement(&state->flutedCount);
        else InterlockedDecrement(&state->flutedCount);
    }
    void Restore() noexcept {
        try {
            if (state) Count(false);
            if (shape.Fill() == effect) {
                updating = true;
                shape.Fill(original);
                updating = false;
            }
            Release();
            if (counted) {
                counted = false;
                if (state) InterlockedDecrement(border ? &state->borderCount : &state->appliedCount);
            }
        } catch (...) {
            updating = false;
            Result(winrt::to_hresult());
        }
    }
    ~Background() {
        if (timer) KillTimer(nullptr, timer);
        if (registered) {
            try { shape.UnregisterPropertyChangedCallback(shapes::Shape::FillProperty(), changedToken); }
            catch (...) { /* Weak callback cannot access a destroyed Background. */ }
        }
        Restore();
    }
};
void RefreshBackgrounds() noexcept {
    try {
        // Docks first: turning the dock off restores its outline's shape
        // before the stroke rectangle gets its stock fill back.
        RefreshDocks();
        // Retain records while Fill callbacks can mutate the visual tree.
        std::vector<std::shared_ptr<Background>> snapshot;
        for (const auto& item : Backgrounds()) snapshot.push_back(item.second);
        for (const auto& background : snapshot) background->Refresh();
        RefreshClocks();
        RefreshVolumes();
        RefreshChevrons();
        RefreshTrays();
        RefreshAdaptive(module,state,AdaptiveOwnerAlive);
    } catch (...) { Result(winrt::to_hresult()); }
}
void CALLBACK BackgroundTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    // All XAML access stays on the UI thread that received this element.
    for (const auto& item : Backgrounds()) {
        if (item.second->timer == timer) {
            const auto strong = item.second;
            strong->Refresh();
            break;
        }
    }
}
class Clock;
auto& Clocks() {
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Clock>>;
    return *records;
}
void CALLBACK ClockTimer(HWND, UINT, UINT_PTR, DWORD);
class Clock {
    ClockView view;
    bool counted = false;
public:
    UINT_PTR timer = 0;
    explicit Clock(const xaml::Controls::StackPanel& panel) : view(panel) {}
    void Start() {
        EnsureBridgeNotifyWindow(module, RefreshBackgrounds);
        timer = SetTimer(nullptr, 0, 1000, ClockTimer);
        if (!timer) winrt::throw_last_error();
        Refresh();
    }
    void Refresh() noexcept {
        try {
            HIGHCONTRASTW contrast{sizeof(contrast)};
            const bool high=!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) || (contrast.dwFlags&HCF_HIGHCONTRASTON);
            view.Refresh(owner.Alive(), DecodeClockOptions(state ? static_cast<UINT>(Read(state->clockOptions)) : 1),state && BareTray(*state) && !high);
            if (state) InterlockedExchange(&state->clockResult, S_OK);
            Count();
        } catch (...) {
            if (state) InterlockedExchange(&state->clockResult, winrt::to_hresult());
            // An unexpected shell layout must leave the stock clock usable.
            try { view.Restore(); } catch (...) {}
            Count();
        }
    }
    void Count() noexcept {
        if (counted == view.Applied()) return;
        counted = view.Applied();
        if (state) {
            if (counted) InterlockedIncrement(&state->clockCount);
            else InterlockedDecrement(&state->clockCount);
        }
    }
    ~Clock() {
        if (timer) KillTimer(nullptr, timer);
        try { view.Restore(); } catch (...) {}
        if (counted && state) InterlockedDecrement(&state->clockCount);
    }
};
void RefreshClocks() noexcept {
    try {
        std::vector<std::shared_ptr<Clock>> snapshot;
        for (const auto& item : Clocks()) snapshot.push_back(item.second);
        for (const auto& clock : snapshot) clock->Refresh();
    } catch (...) { if (state) InterlockedExchange(&state->clockResult, winrt::to_hresult()); }
}
void CALLBACK ClockTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    for (const auto& item : Clocks()) if (item.second->timer == timer) {
        const auto strong = item.second;
        strong->Refresh();
        break;
    }
}
class Volume;
auto& Volumes() {
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Volume>>;
    return *records;
}
void CALLBACK VolumeTimer(HWND, UINT, UINT_PTR, DWORD);
class Volume {
    xaml::FrameworkElement root;
    xaml::Controls::Grid target{nullptr};
    std::unique_ptr<VolumeView> view;
    bool counted = false;
public:
    UINT_PTR timer = 0;
    explicit Volume(const xaml::FrameworkElement& element) : root(element) {}
    void Count() noexcept {
        const bool applied = view && view->Applied();
        if (counted == applied) return;
        counted = applied;
        if (state) {
            if (counted) InterlockedIncrement(&state->volumeCount);
            else InterlockedDecrement(&state->volumeCount);
        }
    }
    void Refresh() noexcept {
        const bool enabled = owner.Alive();
        try {
            if (enabled) {
                // Glyph text can arrive after its Add callback, and the shell
                // can replace the template later. Retry on this root's UI thread.
                const auto grid = FindVolumeGrid(root);
                if (grid && (grid != target || !view)) {
                    view.reset(); Count();
                    target = grid;
                    view = std::make_unique<VolumeView>(grid);
                }
            }
            if (view) view->Refresh(enabled);
            Count();
        }
        catch (...) {
            if (state) InterlockedExchange(&state->volumeResult, winrt::to_hresult());
            try { if (view) view->Restore(); } catch (...) {}
            Count();
        }
    }
    void Start() {
        EnsureBridgeNotifyWindow(module, RefreshBackgrounds);
        timer = SetTimer(nullptr, 0, 2000, VolumeTimer);
        if (!timer) winrt::throw_last_error();
        Refresh();
    }
    ~Volume() {
        if (timer) KillTimer(nullptr, timer);
        try { if (view) view->Restore(); } catch (...) {}
        if (counted && state) InterlockedDecrement(&state->volumeCount);
    }
};
void RefreshVolumes() noexcept {
    try {
        std::vector<std::shared_ptr<Volume>> snapshot;
        for (const auto& item : Volumes()) snapshot.push_back(item.second);
        for (const auto& volume : snapshot) volume->Refresh();
    } catch (...) { if (state) InterlockedExchange(&state->volumeResult, winrt::to_hresult()); }
}
void CALLBACK VolumeTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    for (const auto& item : Volumes()) if (item.second->timer == timer) {
        const auto strong = item.second;
        strong->Refresh();
        break;
    }
}
class Chevron;
auto& Chevrons() {
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Chevron>>;
    return *records;
}
void CALLBACK ChevronTimer(HWND, UINT, UINT_PTR, DWORD);
class Chevron {
    xaml::FrameworkElement root;
    xaml::Controls::TextBlock target{nullptr};
    std::unique_ptr<ChevronView> view;
public:
    UINT_PTR timer = 0;
    explicit Chevron(const xaml::FrameworkElement& element) : root(element) {}
    void Refresh() noexcept {
        try {
            const bool enabled = owner.Alive();
            if (enabled) {
                const auto glyph = FindChevronGlyph(root);
                if (glyph != target || (glyph && !view)) {
                    view.reset(); target = glyph;
                    if (glyph) view = std::make_unique<ChevronView>(glyph);
                }
            }
            if (view) view->Refresh(enabled);
        } catch (...) {
            Result(winrt::to_hresult());
            try { if (view) view->Restore(); } catch (...) {}
        }
    }
    void Start() {
        EnsureBridgeNotifyWindow(module, RefreshBackgrounds);
        timer = SetTimer(nullptr, 0, 2000, ChevronTimer);
        if (!timer) winrt::throw_last_error();
        Refresh();
    }
    ~Chevron() { if (timer) KillTimer(nullptr, timer); }
};
void RefreshChevrons() noexcept {
    try {
        std::vector<std::shared_ptr<Chevron>> snapshot;
        for (const auto& item : Chevrons()) snapshot.push_back(item.second);
        for (const auto& chevron : snapshot) chevron->Refresh();
    } catch (...) { Result(winrt::to_hresult()); }
}
void CALLBACK ChevronTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    for (const auto& item : Chevrons()) if (item.second->timer == timer) {
        const auto strong = item.second;
        strong->Refresh(); break;
    }
}
class Tray;
auto& Trays() {
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Tray>>;
    return *records;
}
void CALLBACK TrayTimer(HWND, UINT, UINT_PTR, DWORD);
// One per taskbar notification area. Changes are applied only from timers and
// refresh messages, never inside Explorer's element-added callback.
class Tray {
    TrayOnlyView view;
    LONG hidden = 0;
    bool found = false;
public:
    UINT_PTR timer = 0, soon = 0;
    explicit Tray(const xaml::FrameworkElement& frame) : view(frame) {}
    void Start() {
        EnsureBridgeNotifyWindow(module, RefreshBackgrounds);
        timer = SetTimer(nullptr, 0, 2000, TrayTimer);
        if (!timer) winrt::throw_last_error();
        Soon();
    }
    // New icons and replaced stacks are hidden shortly after they arrive.
    void Soon() noexcept { if (!soon) soon = SetTimer(nullptr, 0, 50, TrayTimer); }
    void Refresh() noexcept {
        const LONG options = Read(state->trayOptions);
        const bool enabled = owner.Alive() && (options & 1) != 0;
        HRESULT result = S_OK;
        // Bit 1: the menu opens from the compact status bar, so the chevron goes too.
        try { view.Refresh(enabled, (options & 2) != 0); result = view.Error(); }
        catch (...) {
            // An unreadable shell layout must leave the stock tray usable.
            result = winrt::to_hresult();
            view.Restore();
        }
        if (state) InterlockedExchange(&state->trayResult, result);
        Count();
    }
    void Count() noexcept {
        const auto nextHidden = static_cast<LONG>(view.Hidden());
        const bool nextFound = view.Found();
        if (state) {
            if (nextHidden != hidden) InterlockedExchangeAdd(&state->trayHidden, nextHidden - hidden);
            if (nextFound != found) { if (nextFound) InterlockedIncrement(&state->trayFlower); else InterlockedDecrement(&state->trayFlower); }
        }
        hidden = nextHidden; found = nextFound;
    }
    ~Tray() {
        if (timer) KillTimer(nullptr, timer);
        if (soon) KillTimer(nullptr, soon);
        view.Restore();
        Count();
    }
};
void RefreshTrays() noexcept {
    try {
        std::vector<std::shared_ptr<Tray>> snapshot;
        for (const auto& item : Trays()) snapshot.push_back(item.second);
        for (const auto& tray : snapshot) tray->Refresh();
    } catch (...) { if (state) InterlockedExchange(&state->trayResult, winrt::to_hresult()); }
}
void CALLBACK TrayTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    for (const auto& item : Trays()) {
        if (item.second->soon == timer) { KillTimer(nullptr, timer); item.second->soon = 0; }
        else if (item.second->timer != timer) continue;
        const auto strong = item.second;
        strong->Refresh(); break;
    }
}
class Dock;
auto& Docks() {
    thread_local auto* records = new std::unordered_map<InstanceHandle, std::shared_ptr<Dock>>;
    return *records;
}
void CALLBACK DockTimer(HWND, UINT, UINT_PTR, DWORD);
// One per taskbar frame: the floating pill, and a window region that makes
// the rest of the bar click-through. The region is set only from timers and
// refresh messages, never inside a XAML callback.
class Dock : public std::enable_shared_from_this<Dock> {
    xaml::FrameworkElement frame;
    DockView view;
    DockRegion region;
    bool counted = false, shaped = false;
    int remeasures = 0;
public:
    UINT_PTR timer = 0, soon = 0, remeasure = 0;
    explicit Dock(const xaml::FrameworkElement& element) : frame(element), view(element) {}
    void Start() {
        EnsureBridgeNotifyWindow(module, RefreshBackgrounds);
        const std::weak_ptr<Dock> weak = shared_from_this();
        view.OnMoved([weak] { if (const auto self = weak.lock()) self->Soon(); });
        timer = SetTimer(nullptr, 0, 2000, DockTimer);
        if (!timer) winrt::throw_last_error();
        Soon();
    }
    // Buttons added or removed: follow them on the next turn of the loop.
    void Soon() noexcept { if (!soon) soon = SetTimer(nullptr, 0, USER_TIMER_MINIMUM, DockTimer); }
    // The stroke rectangle's stock brush, kept by the acrylic adapter while
    // its own fill is clear. It follows light and dark like the stock line.
    media::Brush StockStroke() const {
        for (const auto& item : Backgrounds()) {
            if (!item.second->border) continue;
            auto parent = media::VisualTreeHelper::GetParent(item.second->shape);
            for (unsigned depth = 0; parent && depth < 16; ++depth, parent = media::VisualTreeHelper::GetParent(parent))
                if (parent == frame) return item.second->original;
        }
        return nullptr;
    }
    HRESULT Shape(bool wanted) noexcept {
        if (!wanted || view.Hits().empty()) { region.Restore(); return S_OK; }
        try {
            if (!frame.XamlRoot()) { region.Restore(); return S_OK; }
            POINT offset{};
            double scale = 1;
            HRESULT error = S_OK;
            const HWND window = FindTaskbarWindow(frame, offset, scale, error);
            if (!window) { region.Restore(); return error; }
            RECT bounds{};
            if (!GetWindowRect(window, &bounds)) { region.Restore(); return HRESULT_FROM_WIN32(GetLastError()); }
            const bool autoHide = (Read(state->dockOptions) & 2) != 0;
            return region.Apply(window, DockRegionRects(view.Hits(), scale, offset, bounds.right - bounds.left, bounds.bottom - bounds.top, autoHide));
        } catch (...) {
            region.Restore();
            return winrt::to_hresult();
        }
    }
    void Refresh() noexcept {
        const bool enabled = owner.Alive() && DockOn(*state);
        HRESULT result = S_OK;
        try {
            view.Refresh(enabled, enabled ? StockStroke() : nullptr, enabled ? Read(state->barHeight) : 0);
            result = view.Error();
        } catch (...) {
            result = winrt::to_hresult();
            view.Restore();
        }
        const HRESULT shapedResult = Shape(enabled && view.Applied());
        if (SUCCEEDED(result)) result = shapedResult;
        if (state) InterlockedExchange(&state->dockResult, result);
        SetHeight();
        Count();
    }
    LONG Height() const noexcept { return view.Height(); }
    // The taller window answers for every taskbar at once (one vtable slot in
    // Taskbar.dll), so it takes the largest height any dock holds.
    void SetHeight() noexcept {
        if (!state) return;
        LONG wanted = 0;
        for (const auto& item : Docks()) wanted = (std::max)(wanted, item.second->Height());
        const LONG before = BarHeight();
        const HRESULT result = SetBarHeight(reinterpret_cast<BYTE*>(GetModuleHandleW(L"Taskbar.dll")),
            static_cast<DWORD>(Read(state->minSizeSlot)), static_cast<DWORD>(Read(state->minSizeFunction)), wanted);
        if (BarHeight() != before) Remeasure();
        InterlockedExchange(&state->barHeightShown, BarHeight());
        InterlockedExchange(&state->barHeightResult, result);
    }
    // Explorer measures the bar again; the second pass, once the frame has
    // its new height, moves the work area too.
    void Remeasure() noexcept {
        remeasures = 2;
        if (!remeasure) remeasure = SetTimer(nullptr, 0, 250, DockTimer);
    }
    void Remeasured() noexcept {
        RemeasureTaskbar();
        if (--remeasures > 0) return;
        KillTimer(nullptr, remeasure);
        remeasure = 0;
    }
    void Count() noexcept {
        if (!state) return;
        if (counted != view.Applied()) { counted = view.Applied(); counted ? InterlockedIncrement(&state->dockCount) : InterlockedDecrement(&state->dockCount); }
        if (shaped != region.Applied()) { shaped = region.Applied(); shaped ? InterlockedIncrement(&state->dockRegions) : InterlockedDecrement(&state->dockRegions); }
    }
    ~Dock() {
        if (timer) KillTimer(nullptr, timer);
        if (soon) KillTimer(nullptr, soon);
        if (remeasure) KillTimer(nullptr, remeasure);
        region.Restore();
        view.Restore();
        Count();
    }
};
void RefreshDocks() noexcept {
    try {
        std::vector<std::shared_ptr<Dock>> snapshot;
        for (const auto& item : Docks()) snapshot.push_back(item.second);
        for (const auto& dock : snapshot) dock->Refresh();
    } catch (...) { if (state) InterlockedExchange(&state->dockResult, winrt::to_hresult()); }
}
void SoonDocks() noexcept {
    for (const auto& item : Docks()) item.second->Soon();
}
void CALLBACK DockTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    for (const auto& item : Docks()) {
        if (item.second->remeasure == timer) { const auto strong = item.second; strong->Remeasured(); break; }
        if (item.second->soon == timer) { KillTimer(nullptr, timer); item.second->soon = 0; }
        else if (item.second->timer != timer) continue;
        const auto strong = item.second;
        strong->Refresh(); break;
    }
}
struct Strings {
    VisualElement element;
    ~Strings() {
        SysFreeString(element.Name);
        SysFreeString(element.Type);
        SysFreeString(element.SrcInfo.FileName);
        SysFreeString(element.SrcInfo.Hash);
    }
};
struct Watcher : winrt::implements<Watcher, IVisualTreeServiceCallback2, winrt::non_agile> {
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    explicit Watcher(IUnknown* site) {
        winrt::check_hresult(site->QueryInterface(__uuidof(IXamlDiagnostics), diagnostics.put_void()));
    }
    bool IsTaskbarBackground(const shapes::Rectangle& shape) {
        auto parent = media::VisualTreeHelper::GetParent(shape);
        for (unsigned depth = 0; parent && depth < 32; ++depth) {
            if (winrt::get_class_name(parent) == L"Taskbar.TaskbarFrame") return true;
            parent = media::VisualTreeHelper::GetParent(parent);
        }
        return false;
    }
    void TryClock(const xaml::DependencyObject& object) {
        auto panel = object.try_as<xaml::Controls::StackPanel>();
        if (!panel) panel = media::VisualTreeHelper::GetParent(object).try_as<xaml::Controls::StackPanel>();
        if (!IsClockPanel(panel)) return;
        auto parent = media::VisualTreeHelper::GetParent(panel);
        bool clockContent = false;
        for (unsigned depth = 0; parent && depth < 32; ++depth) {
            if (winrt::get_class_name(parent) == L"SystemTray.DateTimeIconContent") { clockContent = true; break; }
            parent = media::VisualTreeHelper::GetParent(parent);
        }
        if (!clockContent) return;
        InstanceHandle handle = 0;
        winrt::check_hresult(diagnostics->GetHandleFromIInspectable(
            reinterpret_cast<::IInspectable*>(winrt::get_abi(panel)), &handle));
        auto& clocks = Clocks();
        if (clocks.find(handle) != clocks.end()) return;
        auto clock = std::make_shared<Clock>(panel);
        clocks.emplace(handle, clock);
        try { clock->Start(); }
        catch (...) { clocks.erase(handle); throw; }
    }
    void TryVolume(const xaml::DependencyObject& object) {
        const auto root = FindVolumeRoot(object);
        if (!root) return;
        InstanceHandle handle = 0;
        winrt::check_hresult(diagnostics->GetHandleFromIInspectable(
            reinterpret_cast<::IInspectable*>(winrt::get_abi(root)), &handle));
        auto& volumes = Volumes();
        if (volumes.find(handle) != volumes.end()) return;
        auto volume = std::make_shared<Volume>(root);
        volumes.emplace(handle, volume);
        try { volume->Start(); }
        catch (...) { volumes.erase(handle); throw; }
    }
    void TryChevron(const xaml::DependencyObject& object) {
        const auto root = FindChevronRoot(object);
        if (!root) return;
        InstanceHandle handle = 0;
        winrt::check_hresult(diagnostics->GetHandleFromIInspectable(
            reinterpret_cast<::IInspectable*>(winrt::get_abi(root)), &handle));
        auto& chevrons = Chevrons();
        const auto found = chevrons.find(handle);
        if (found != chevrons.end()) { const auto strong = found->second; strong->Refresh(); return; }
        auto chevron = std::make_shared<Chevron>(root);
        chevrons.emplace(handle, chevron);
        try { chevron->Start(); }
        catch (...) { chevrons.erase(handle); throw; }
    }
    void TryDock(const xaml::DependencyObject& object, InstanceHandle handle) {
        const auto frame = object.try_as<xaml::FrameworkElement>();
        if (!frame) return;
        auto& docks = Docks();
        if (docks.find(handle) != docks.end()) return;
        auto dock = std::make_shared<Dock>(frame);
        docks.emplace(handle, dock);
        try { dock->Start(); }
        catch (...) { docks.erase(handle); throw; }
    }
    void TryTray(const xaml::DependencyObject& object) {
        const auto frame = FindTrayFrame(object);
        if (!frame) return;
        InstanceHandle handle = 0;
        winrt::check_hresult(diagnostics->GetHandleFromIInspectable(
            reinterpret_cast<::IInspectable*>(winrt::get_abi(frame)), &handle));
        auto& trays = Trays();
        const auto found = trays.find(handle);
        if (found != trays.end()) { found->second->Soon(); return; }
        auto tray = std::make_shared<Tray>(frame);
        trays.emplace(handle, tray);
        try { tray->Start(); }
        catch (...) { trays.erase(handle); throw; }
    }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
        VisualMutationType mutation) noexcept override {
        try {
            // Only Handle is valid on Remove. Never read/free other fields there.
            if (mutation == Remove) {
                RemoveAdaptive(element.Handle);
                auto& backgrounds = Backgrounds();
                const auto it = backgrounds.find(element.Handle);
                if (it != backgrounds.end()) {
                    // Release XAML references after the map mutation has completed.
                    const auto strong = it->second;
                    backgrounds.erase(it);
                }
                auto& clocks = Clocks();
                const auto clock = clocks.find(element.Handle);
                if (clock != clocks.end()) {
                    const auto strong = clock->second;
                    clocks.erase(clock);
                }
                auto& volumes = Volumes();
                const auto volume = volumes.find(element.Handle);
                if (volume != volumes.end()) {
                    const auto strong = volume->second;
                    volumes.erase(volume);
                }
                auto& chevrons = Chevrons();
                const auto chevron = chevrons.find(element.Handle);
                if (chevron != chevrons.end()) {
                    const auto strong = chevron->second;
                    chevrons.erase(chevron);
                }
                auto& trays = Trays();
                const auto tray = trays.find(element.Handle);
                if (tray != trays.end()) {
                    const auto strong = tray->second;
                    trays.erase(tray);
                }
                auto& docks = Docks();
                const auto dock = docks.find(element.Handle);
                if (dock != docks.end()) {
                    const auto strong = dock->second;
                    docks.erase(dock);
                }
                return S_OK;
            }
            if (mutation != Add) return S_OK;
            Strings strings{element};
            if (!element.Type) return S_OK;
            const std::wstring_view type(element.Type);
            if(type==L"Windows.UI.Xaml.Controls.TextBlock" || type==L"Windows.UI.Xaml.Controls.FontIcon" || type==L"Windows.UI.Xaml.Controls.SymbolIcon" || type==L"Windows.UI.Xaml.Controls.Image" || type==L"Windows.UI.Xaml.Shapes.Path"){
                try{winrt::Windows::Foundation::IInspectable object{nullptr};winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    RefreshAdaptive(module,state,AdaptiveOwnerAlive);TrackAdaptive(element.Handle,object.as<xaml::DependencyObject>());
                }catch(...){if(state)InterlockedExchange(&state->adaptiveResult,winrt::to_hresult());}
            }
            if (type == L"Taskbar.TaskbarFrame") {
                try {
                    winrt::Windows::Foundation::IInspectable object{nullptr};
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    TryDock(object.as<xaml::DependencyObject>(), element.Handle);
                } catch (...) { if (state) InterlockedExchange(&state->dockResult, winrt::to_hresult()); }
                return S_OK;
            }
            const bool trayCandidate = type == L"SystemTray.SystemTrayFrame" || type == L"SystemTray.NotifyIconView"
                || type == L"SystemTray.Stack" || type == L"SystemTray.OmniButton";
            if (trayCandidate) {
                try {
                    winrt::Windows::Foundation::IInspectable object{nullptr};
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    TryTray(object.as<xaml::DependencyObject>());
                } catch (...) { if (state) InterlockedExchange(&state->trayResult, winrt::to_hresult()); }
            }
            const bool chevronCandidate = type == L"SystemTray.ChevronIconView"
                || (type == L"Windows.UI.Xaml.Controls.TextBlock" && element.Name
                    && std::wstring_view(element.Name) == L"InnerTextBlock");
            if (chevronCandidate) {
                try {
                    winrt::Windows::Foundation::IInspectable object{nullptr};
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    TryChevron(object.as<xaml::DependencyObject>());
                } catch (...) { Result(winrt::to_hresult()); }
            }
            const bool volumeCandidate = type == L"SystemTray.OmniButton" || type == L"Windows.UI.Xaml.Controls.Grid"
                || type == L"Windows.UI.Xaml.Controls.TextBlock" || type == L"Windows.UI.Xaml.Controls.FontIcon";
            if (volumeCandidate) {
                try {
                    winrt::Windows::Foundation::IInspectable object{nullptr};
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    TryVolume(object.as<xaml::DependencyObject>());
                } catch (...) { if (state) InterlockedExchange(&state->volumeResult, winrt::to_hresult()); }
            }
            const bool clockCandidate = type == L"Windows.UI.Xaml.Controls.StackPanel"
                || (type == L"Windows.UI.Xaml.Controls.TextBlock" && element.Name
                    && (std::wstring_view(element.Name) == L"DateInnerTextBlock" || std::wstring_view(element.Name) == L"TimeInnerTextBlock"));
            if (clockCandidate) {
                try {
                    winrt::Windows::Foundation::IInspectable object{nullptr};
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    TryClock(object.as<xaml::DependencyObject>());
                } catch (...) { if (state) InterlockedExchange(&state->clockResult, winrt::to_hresult()); }
                return S_OK;
            }
            if (!element.Name || type != L"Windows.UI.Xaml.Shapes.Rectangle") return S_OK;
            const std::wstring_view name(element.Name);
            const bool border = name == L"BackgroundStroke";
            if (!border && name != L"BackgroundFill") return S_OK;
            winrt::Windows::Foundation::IInspectable object{nullptr};
            winrt::check_hresult(diagnostics->GetIInspectableFromHandle(element.Handle,
                reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
            const auto shape = object.try_as<shapes::Rectangle>();
            if (!shape || !IsTaskbarBackground(shape)) return S_OK;
            auto& backgrounds = Backgrounds();
            if (backgrounds.find(element.Handle) == backgrounds.end()) {
                auto background = std::make_shared<Background>(shape, border);
                backgrounds.emplace(element.Handle, background);
                try { background->Start(); }
                catch (...) { backgrounds.erase(element.Handle); throw; }
                // A frame seen before its background can take its shape now.
                SoonDocks();
            }
        } catch (...) { Result(winrt::to_hresult()); }
        // A shell change/error must not propagate into Explorer's diagnostics.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }
};
struct Site : winrt::implements<Site, IObjectWithSite, winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;
    winrt::com_ptr<Watcher> watcher;
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override {
        try {
            if (!value) { site = nullptr; return S_OK; }
            if (watcher) return E_UNEXPECTED;
            site.copy_from(value);
            watcher = winrt::make_self<Watcher>(value);
            // Advise synchronously dispatches callbacks on XAML UI threads.
            // Calling it off the SetSite thread avoids a shell deadlock.
            std::thread([strong = watcher] {
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    winrt::check_hresult(strong->diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(strong.get()));
                    if (state) InterlockedExchange(&state->bridgeState, Attached);
                    // Do not clear an element-application error set by callbacks.
                    winrt::uninit_apartment();
                } catch (...) {
                    Result(winrt::to_hresult());
                    if (state) InterlockedExchange(&state->bridgeState, Failed);
                    booting = false;
                }
            }).detach();
            return S_OK;
        } catch (...) { booting = false; return winrt::to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return site ? site->QueryInterface(iid, output) : E_FAIL;
    }
};
struct Factory : winrt::implements<Factory, IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try { return winrt::make_self<Site>()->QueryInterface(iid, output); }
        catch (...) { return winrt::to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};

void Bootstrap() noexcept {
    HRESULT hr = E_FAIL;
    HMODULE xamlLibrary = nullptr;
    try {
        xamlLibrary = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!xamlLibrary) winrt::throw_last_error();
        const auto initialize = reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(
            GetProcAddress(xamlLibrary, "InitializeXamlDiagnosticsEx"));
        if (!initialize) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        if (!length || length >= std::size(path)) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME));
        for (unsigned attempt = 0; attempt < 60; ++attempt) {
            if (!state || !Read(state->running)) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); break; }
            const auto endpoint = L"VisualDiagConnection" + std::to_wstring(++endpointSerial);
            // Diagnostics may initialize only once per calling thread.
            std::thread([&hr, initialize, endpoint, &path] {
                hr = initialize(endpoint.c_str(), GetCurrentProcessId(), nullptr, path, TapClass, nullptr);
            }).join();
            if (SUCCEEDED(hr)) {
                FreeLibrary(xamlLibrary);
                return; // Site's asynchronous Advise reports readiness/failure.
            }
            Sleep(500);
        }
    } catch (...) { hr = winrt::to_hresult(); }
    if (xamlLibrary) FreeLibrary(xamlLibrary);
    Result(hr);
    if (state) InterlockedExchange(&state->bridgeState, Failed);
    booting = false;
}
} // namespace

extern "C" LRESULT CALLBACK AcrylicHookProc(int code, WPARAM wParam, LPARAM lParam) noexcept {
    if (code >= 0 && !booting.exchange(true)) {
        try {
            if (!state) {
                mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName(GetCurrentProcessId()).c_str()));
                if (!mapping) { booting = false; return CallNextHookEx(nullptr, code, wParam, lParam); }
                state = static_cast<SharedState*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
            }
            if (!state || Read(state->magic) != Magic || !Read(state->running)) booting = false;
            else if (Read(state->bridgeState) != Attached) {
                // Pin before launching threads: the diagnostics API and timers
                // retain code pointers until Explorer exits. No work in DllMain.
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&AcrylicHookProc), &module)) winrt::throw_last_error();
                InterlockedExchange(&state->bridgeState, Starting);
                Result(S_OK);
                std::thread(Bootstrap).detach();
            }
        } catch (...) {
            Result(winrt::to_hresult());
            if (state) InterlockedExchange(&state->bridgeState, Failed);
            booting = false;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (!IsEqualCLSID(clsid, TapClass)) return CLASS_E_CLASSNOTAVAILABLE;
    try { return winrt::make_self<Factory>()->QueryInterface(iid, output); }
    catch (...) { return winrt::to_hresult(); }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return S_FALSE; // A pinned TAP DLL and UI-thread callbacks must remain resident.
}

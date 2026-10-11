// SPDX-License-Identifier: GPL-3.0-or-later
// Taskbar XAML identification and Backdrop acrylic approach are adapted from
// TranslucentTB's ExplorerTAP. See THIRD_PARTY_NOTICES.md for pinned sources.
#include "shared.hpp"
#include "bridge_notify.hpp"
#include "theme.hpp"
#include "clock.hpp"
#include "volume.hpp"
#include "chevron.hpp"
#include "acrylic_material.hpp"
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
#include <atomic>
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

class Background : public std::enable_shared_from_this<Background> {
public:
    shapes::Rectangle shape;
    media::Brush original{nullptr};
    media::AcrylicBrush acrylic{nullptr};
    media::Brush effect{nullptr};
    bool border = false, light = false;
    AcrylicMode mode = AcrylicMode::Off;
    UINT_PTR timer = 0;
    int64_t changedToken = 0;
    bool registered = false, updating = false, counted = false;

    explicit Background(shapes::Rectangle rectangle, bool isBorder) : shape(std::move(rectangle)), original(shape.Fill()), border(isBorder) {
        if (border) {
            effect = media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0});
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
            if (owner.Alive() && Read(state->active) != 0) {
                if (!border && (light != SystemLightTheme() || mode != DecodeAcrylicMode(Read(state->active)))) UpdateTheme();
                if (shape.Fill() != effect) {
                    updating = true;
                    shape.Fill(effect);
                    updating = false;
                }
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
    void Restore() noexcept {
        try {
            if (shape.Fill() == effect) {
                updating = true;
                shape.Fill(original);
                updating = false;
            }
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
        // Retain records while Fill callbacks can mutate the visual tree.
        std::vector<std::shared_ptr<Background>> snapshot;
        for (const auto& item : Backgrounds()) snapshot.push_back(item.second);
        for (const auto& background : snapshot) background->Refresh();
        RefreshClocks();
        RefreshVolumes();
        RefreshChevrons();
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
            view.Refresh(owner.Alive(), DecodeClockOptions(state ? static_cast<UINT>(Read(state->clockOptions)) : 1));
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
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
        VisualMutationType mutation) noexcept override {
        try {
            // Only Handle is valid on Remove. Never read/free other fields there.
            if (mutation == Remove) {
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
                return S_OK;
            }
            if (mutation != Add) return S_OK;
            Strings strings{element};
            if (!element.Type) return S_OK;
            const std::wstring_view type(element.Type);
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

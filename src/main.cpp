// SPDX-License-Identifier: GPL-3.0-or-later
#include "shared.hpp"
#include "bridge_notify.hpp"
#include "taskbar_probe.hpp"
#include "clock_options.hpp"
#include "tray.hpp"
#include "cursors.hpp"
#include "theme.hpp"
#include "auto_theme.hpp"
#include <winrt/base.h>
#include "popup.hpp"
#include "updates.hpp"
#include "update_download.hpp"
#include "version.hpp"
#include "startup.hpp"
#include "wallpaper.hpp"
#include "wallpaper_theme.hpp"
#include "taskbar_options.hpp"
#include "start_session.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <vector>
#include <cwchar>

using namespace acrylic;
namespace {
constexpr UINT_PTR RefreshTimer = 1;
constexpr UINT_PTR DebounceTimer = 2;
constexpr UINT_PTR UpdateTimer = 3;
constexpr UINT_PTR FastAttachTimer = 4;
constexpr UINT_PTR AutoThemeTimer = 5;
constexpr UINT_PTR WallpaperAutoThemeTimer = 6;
constexpr UINT RefreshMs = 5000;
struct AccentPolicy { int state; UINT flags; DWORD color; DWORD animation; };
struct CompositionData { int attribute; void* data; SIZE_T size; };
static_assert(sizeof(AccentPolicy) == 16);
using SetComposition = BOOL(WINAPI*)(HWND, const CompositionData*);

std::wstring ExePath() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    return {path.data(), length};
}
DWORD BuildNumber() {
    // RtlGetVersion avoids version-lie compatibility shims.
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto fn = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return fn && fn(&version) >= 0 ? version.dwBuildNumber : 0;
}
bool IsExplorer(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (!process || !QueryFullProcessImageNameW(process.get(), 0, path, &length)) return false;
    const wchar_t* filename = wcsrchr(path, L'\\');
    return _wcsicmp(filename ? filename + 1 : path, L"explorer.exe") == 0;
}
std::vector<HWND> Taskbars() {
    std::vector<HWND> result;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(window, name, static_cast<int>(std::size(name)));
        if ((wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0)
            && IsExplorer(window)) {
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::wstring LogPath() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    std::wstring folder = std::wstring(local) + L"\\jellyflower";
    CoTaskMemFree(local);
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\diagnostics.log";
}
void Log(const std::wstring& message) {
    const auto path = LogPath();
    if (path.empty()) return;
    Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) && size.QuadPart > 65536) {
        file.reset(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t stamp[64]{};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", time.wYear, time.wMonth,
        time.wDay, time.wHour, time.wMinute, time.wSecond);
    const auto text = std::wstring(stamp) + message + L"\r\n";
    const int sizeUtf8 = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(sizeUtf8), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), sizeUtf8, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}
DWORD Startup(bool install) { return RegisterStartup(ExePath(), install); }
DWORD Stop() {
    const HWND window = FindWindowW(WindowClass, nullptr);
    if (!window) return ERROR_SUCCESS;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    Handle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (!PostMessageW(window, WM_CLOSE, 0, 0)) return GetLastError();
    if (!process) return ERROR_ACCESS_DENIED;
    return WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0 ? ERROR_SUCCESS : WAIT_TIMEOUT;
}
DWORD Diagnose(const wchar_t* output) {
    const HWND window = FindWindowW(WindowClass, nullptr);
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    LONG values[32]{};
    bool responsive = window != nullptr;
    for (WPARAM query = 0; query < std::size(values) && window; ++query) {
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(window, QueryMessage, query, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result)) {
            responsive = false;
            break;
        }
        values[query] = static_cast<LONG>(result);
    }
    SYSTEM_POWER_STATUS power{};
    const bool powerKnown = GetSystemPowerStatus(&power) != FALSE && power.SystemStatusFlag != 255;
    std::ostringstream json;
    json << "{\n  \"running\": " << (window ? "true" : "false")
        << ",\n  \"responsive\": " << (responsive ? "true" : "false")
        << ",\n  \"pid\": " << pid << ",\n  \"windowsBuild\": " << BuildNumber()
        << ",\n  \"batterySaver\": " << (powerKnown ? (power.SystemStatusFlag ? "true" : "false") : "null")
        << ",\n  \"backend\": \"" << (values[0] == Modern ? "xaml" : values[0] == Legacy ? "accent" :
            values[0] == Unsupported ? "unsupported" : "waiting") << "\""
        << ",\n  \"appliedBackgrounds\": " << values[1]
        << ",\n  \"lastResult\": " << values[2]
        << ",\n  \"bridgeState\": " << values[3]
        << ",\n  \"acrylicEnabled\": " << (values[4] ? "true" : "false")
        << ",\n  \"trayIconPresent\": " << (values[5] ? "true" : "false")
        << ",\n  \"cursorMode\": \"";
    const wchar_t* cursorName = CursorManager::Name(static_cast<UINT>(values[6]));
    while (*cursorName) json << static_cast<char>(*cursorName++);
    json << "\""
        << ",\n  \"cursorApplied\": " << (values[7] ? "true" : "false")
        << ",\n  \"cursorLastError\": " << values[8]
        << ",\n  \"taskbarBordersHidden\": " << values[9]
        << ",\n  \"lightMode\": " << (values[10] ? "true" : "false")
        << ",\n  \"acrylicMode\": \"" << (values[11] == 2 ? "pure" : values[11] == 1 ? "system" : "off") << "\""
        << ",\n  \"singleLineClocks\": " << values[12]
        << ",\n  \"clockLastResult\": " << values[13]
        << ",\n  \"hiddenVolumeIcons\": " << values[14]
        << ",\n  \"volumeLastResult\": " << values[15]
        << ",\n  \"taskbarXamlIslands\": " << values[16]
        << ",\n  \"bridgeRetained\": " << (values[17] ? "true" : "false")
        << ",\n  \"clockDate\": " << (values[18] ? "true" : "false")
        << ",\n  \"startSurfaces\": " << values[19]
        << ",\n  \"startAttached\": " << values[20]
        << ",\n  \"startLastResult\": " << values[21]
        << ",\n  \"startSearchHeaders\": " << values[22]
        << ",\n  \"startSpacingOverrides\": " << values[23]
        << ",\n  \"searchAttached\": " << values[24]
        << ",\n  \"searchInputHeaders\": " << values[25]
        << ",\n  \"searchLastResult\": " << values[26]
        << ",\n  \"wallpaperStatus\": " << values[27]
        << ",\n  \"wallpaperLastResult\": " << values[28]
        << ",\n  \"adaptiveTrayGlyphs\": " << values[29]
        << ",\n  \"adaptiveTrayBitmaps\": " << values[30]
        << ",\n  \"adaptiveLastResult\": " << values[31]
        << ",\n  \"visualBlurVerified\": false\n}\n";
    Handle file(CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return GetLastError();
    const auto bytes = json.str();
    DWORD written = 0;
    if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) return GetLastError();
    return written == bytes.size() ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}

class Controller {
public:
    HWND window = nullptr;
    AcrylicMode mode = AcrylicMode::System;
    TrayIcon tray;
    StartSession start;
    StartSession search{ShellHost::Search};
    UpdateChecker updates;
    std::unique_ptr<AutoTheme> autoTheme;
    std::unique_ptr<WallpaperManager> wallpaper;
    std::unique_ptr<AutoTheme> wallpaperAutoTheme;
    WallpaperTheme wallpaperTheme=LoadWallpaperTheme();
    bool wallpaperLight=SystemLightTheme();
    AutoThemeStatus lastWallpaperAutoStatus=AutoThemeStatus::Manual;
    void RefreshWallpaperTheme(bool invalidate=false,bool requestLocation=false,bool force=false){
        KillTimer(window,WallpaperAutoThemeTimer);
        bool solarLight=false,solarReady=false;UINT delay=0;
        const auto status=wallpaper?wallpaper->Status():WallpaperStatus::Off;
        const bool active=status==WallpaperStatus::On || status==WallpaperStatus::Applying || status==WallpaperStatus::ErrorOn;
        if(wallpaperTheme==WallpaperTheme::Auto && active){
            if(!wallpaperAutoTheme)wallpaperAutoTheme=std::make_unique<AutoTheme>(window,AutoThemeScope::Wallpaper);
            if(requestLocation && wallpaperAutoTheme->Status()!=AutoThemeStatus::Automatic && wallpaperAutoTheme->Status()!=AutoThemeStatus::Locating)wallpaperAutoTheme->Enable();
            solarReady=wallpaperAutoTheme->Update(solarLight,delay,invalidate);
            const auto current=wallpaperAutoTheme->Status();
            if(current!=lastWallpaperAutoStatus){lastWallpaperAutoStatus=current;Log(L"Wallpaper auto status="+std::to_wstring(static_cast<UINT>(current))+L" error="+std::to_wstring(wallpaperAutoTheme->Error()));}
        }else{wallpaperAutoTheme.reset();lastWallpaperAutoStatus=AutoThemeStatus::Manual;}
        const bool next=WallpaperUsesLight(wallpaperTheme,SystemLightTheme(),solarReady,solarLight);
        // Completion messages must not queue another identical worker request.
        // Only a changed decision or an external desktop/display event refreshes.
        if(wallpaper && (force || next!=wallpaperLight))wallpaper->Refresh(next);
        wallpaperLight=next;
        if(delay && !SetTimer(window,WallpaperAutoThemeTimer,delay,nullptr))Log(L"Wallpaper auto timer error="+std::to_wstring(GetLastError()));
    }
    UINT ChangeWallpaperTheme(){
        const auto next=wallpaperTheme==WallpaperTheme::System?WallpaperTheme::Auto:WallpaperTheme::System;
        const auto error=SaveWallpaperTheme(next);
        if(error){Log(L"Wallpaper theme setting error="+std::to_wstring(error));return static_cast<UINT>(wallpaperTheme);}
        wallpaperTheme=next;
        if(next==WallpaperTheme::System && wallpaperAutoTheme){const auto disabled=wallpaperAutoTheme->Disable();if(disabled)Log(L"Wallpaper location cleanup error="+std::to_wstring(disabled));}
        RefreshWallpaperTheme(false,true);
        RefreshWallpaperMenu(window);
        return static_cast<UINT>(wallpaperTheme);
    }
    AutoThemeStatus lastAutoStatus = AutoThemeStatus::Manual;
    void RefreshAutoTheme(bool invalidate = false) {
        bool light = false;
        UINT delay = 0;
        if (!autoTheme) return;
        KillTimer(window, AutoThemeTimer);
        if (autoTheme->Update(light, delay, invalidate) && (SystemLightTheme() != light || AppLightTheme() != light)) {
            const auto changed = SetWindowsLightTheme(light);
            if (changed.error) Log(L"Automatic theme error=" + std::to_wstring(changed.error));
            RefreshWallpaperTheme();
        }
        // A non-waking window timer. Re-arm from the next absolute deadline,
        // rather than running solar calculations in the taskbar recovery loop.
        if (delay && !SetTimer(window, AutoThemeTimer, delay, nullptr))
            Log(L"Auto theme timer error=" + std::to_wstring(GetLastError()));
        const auto state = autoTheme->Status();
        if (state != lastAutoStatus) {
            lastAutoStatus = state;
            Log(L"Auto theme status=" + std::to_wstring(static_cast<UINT>(state)) + L" error=" + std::to_wstring(autoTheme->Error()));
            RefreshThemeMenu(window);
        }
    }
    std::unique_ptr<CursorManager> cursors;
    ClockOptions clock = LoadClockOptions();
    DWORD build = BuildNumber();
    LONG backend = Waiting, applied = 0, lastResult = S_OK;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    HPOWERNOTIFY powerNotification = nullptr;
    SetComposition setComposition = reinterpret_cast<SetComposition>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    HMODULE bridgeDll = nullptr;
    HHOOK bridgeHook = nullptr;
    HWINEVENTHOOK foregroundHook = nullptr;
    Handle mapping;
    Handle explorerProcess;
    SharedState* shared = nullptr;
    DWORD explorerPid = 0;
    LONG previousBridgeState = Idle, previousBridgeResult = S_OK;
    unsigned observedIslands = 0;
    ULONGLONG attachStarted = GetTickCount64();
    bool firstAppearanceLogged = false;
    bool AppearanceReady() const {
        return backend == Legacy ? applied > 0 : shared && Read(shared->bridgeState) == Attached
            && Read(shared->appliedCount) > 0;
    }
    void FastAttach() {
        RefreshTaskbar();
        const bool ready = AppearanceReady();
        if (ready && !firstAppearanceLogged) {
            firstAppearanceLogged = true;
            Log(L"First taskbar appearance request succeeded after ms=" + std::to_wstring(GetTickCount64() - attachStarted));
        }
        if (ShellRetryInterval(GetTickCount64() - attachStarted, ready) != 100) KillTimer(window, FastAttachTimer);
    }
    bool bridgeRetained = false;
    std::vector<HWND> changedLegacyTaskbars;

    void ReleaseBridge() {
        if (shared) InterlockedExchange(&shared->running, 0);
        SetBridgeMode(shared, explorerPid, AcrylicMode::Off, true);
        if (bridgeHook) { UnhookWindowsHookEx(bridgeHook); bridgeHook = nullptr; }
        if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
        mapping.reset();
        explorerProcess.reset();
        explorerPid = 0;
    }
    void StartBridge(HWND primary) {
        DWORD pid = 0;
        const DWORD tid = GetWindowThreadProcessId(primary, &pid);
        if (!tid || !pid) return; // A disappearing HWND must not end a live session.
        const bool newOwner = pid != explorerPid || !shared || !explorerProcess
            || WaitForSingleObject(explorerProcess.get(), 0) != WAIT_TIMEOUT;
        if (newOwner) ReleaseBridge();
        // An older pinned helper must be removed by restarting Explorer before
        // a different brush implementation attaches to the same rectangles.
        if (newOwner && OlderBridgePresent(pid)) { lastResult = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH); return; }
        if (newOwner) {
            explorerPid = pid;
            explorerProcess.reset(OpenProcess(SYNCHRONIZE, FALSE, pid));
            if (!explorerProcess) { lastResult = HRESULT_FROM_WIN32(GetLastError()); return; }
            mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                sizeof(SharedState), MappingName(pid).c_str()));
            if (!mapping) { lastResult = HRESULT_FROM_WIN32(GetLastError()); return; }
            shared = static_cast<SharedState*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
            if (!shared) { lastResult = HRESULT_FROM_WIN32(GetLastError()); return; }
            InterlockedExchange(&shared->magic, Magic);
            InterlockedExchange(&shared->controllerPid, static_cast<LONG>(GetCurrentProcessId()));
            InterlockedIncrement(&shared->epoch);
            InterlockedExchange(&shared->running, 1);
            previousBridgeState = Idle;
        }
        InterlockedExchange(&shared->clockOptions, static_cast<LONG>(clock.Packed()));
        SetBridgeMode(shared, explorerPid, mode, newOwner);
        if (!bridgeDll) {
            auto path = ExePath();
            const auto slash = path.find_last_of(L'\\');
            if (slash == std::wstring::npos) { lastResult = E_FAIL; return; }
            path.resize(slash + 1);
            path += L"jellyflower-bridge-v16.dll";
            bridgeDll = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!bridgeDll) { lastResult = HRESULT_FROM_WIN32(GetLastError()); return; }
        }
        if (Read(shared->bridgeState) == Attached) {
            if (bridgeHook) { UnhookWindowsHookEx(bridgeHook); bridgeHook = nullptr; }
        } else if (!bridgeHook) {
            const auto hookProc = reinterpret_cast<HOOKPROC>(GetProcAddress(bridgeDll, "AcrylicHookProc"));
            if (!hookProc) { lastResult = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND); return; }
            bridgeHook = SetWindowsHookExW(WH_CALLWNDPROC, hookProc, bridgeDll, tid);
            if (!bridgeHook) { lastResult = HRESULT_FROM_WIN32(GetLastError()); return; }
        }
        if (bridgeHook) {
            DWORD_PTR ignored = 0;
            // Forces a hook callback even when Explorer's taskbar thread is idle.
            SendMessageTimeoutW(primary, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &ignored);
        }
        const LONG state = Read(shared->bridgeState);
        lastResult = Read(shared->lastResult);
        applied = Read(shared->appliedCount);
        if (state != previousBridgeState || lastResult != previousBridgeResult) {
            Log(L"XAML bridge state=" + std::to_wstring(state) + L" result=" + std::to_wstring(lastResult));
            previousBridgeState = state;
            previousBridgeResult = lastResult;
        }
    }
    void RestoreLegacy() {
        for (const HWND taskbar : changedLegacyTaskbars) {
            wchar_t name[64]{};
            GetClassNameW(taskbar, name, static_cast<int>(std::size(name)));
            if ((wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0)
                && IsWindow(taskbar) && IsExplorer(taskbar)) {
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(taskbar, WM_DWMCOMPOSITIONCHANGED, 1, 0,
                    SMTO_ABORTIFHUNG, 200, &ignored);
            }
        }
        changedLegacyTaskbars.clear();
    }
    void Refresh() {
        RefreshTaskbar();
        start.Refresh(SystemLightTheme(),window);
        search.Refresh(SystemLightTheme(),window);
        tray.Refresh(window, mode);
        const DWORD cursorBefore = cursors->Error();
        cursors->Refresh();
        DWORD importError = 0;
        if (cursors->TakeImportResult(importError)) {
            Log(L"Cursor ZIP import error=" + std::to_wstring(importError));
            if (importError) tray.UpdateNotice(L"cursor zip could not be imported",
                L"Use the original, complete Cursor Concept 3 HDPi ZIP. Choose colors / unlock for instructions. Your current cursor is unchanged.", true);
            else tray.UpdateNotice(L"cursor colors unlocked", L"Your pack is ready. Open cursor to choose a color or follow Windows light/dark mode.");
            RefreshCursorMenu(window);
        }
        if (cursors->Error() != cursorBefore && cursors->Error()) {
            Log(L"Cursor error=" + std::to_wstring(cursors->Error()));
            tray.CursorError(cursors->Error(), cursors->SaveFailed());
        }
    }
    void RefreshTaskbar() {
        HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (primary && !IsExplorer(primary)) primary = nullptr;
        const auto probe = ProbeTaskbar(build, primary, shared ? explorerProcess.get() : nullptr, explorerPid);
        const LONG selected = probe.backend;
        if (observedIslands != probe.islands && probe.retained)
            Log(L"Retained live XAML bridge; taskbar islands=" + std::to_wstring(probe.islands));
        observedIslands = probe.islands;
        bridgeRetained = probe.retained;
        if (backend != selected) {
            if (backend == Legacy) RestoreLegacy();
            if (selected != Modern) ReleaseBridge();
            backend = selected;
            Log(L"Backend=" + std::to_wstring(backend) + L" Windows build=" + std::to_wstring(build)
                + L" taskbar islands=" + std::to_wstring(observedIslands));
        }
        applied = 0;
        if (mode == AcrylicMode::Off && backend != Modern) {
            RestoreLegacy();
            return;
        }
        if (backend == Modern) {
            const LONG previous = lastResult;
            if (primary) StartBridge(primary);
            else if (shared) {
                // The shell is rebuilding its HWNDs; publish settings without
                // ending ownership or restoring the surviving XAML elements.
                InterlockedExchange(&shared->clockOptions, static_cast<LONG>(clock.Packed()));
                SetBridgeMode(shared, explorerPid, mode);
                applied = Read(shared->appliedCount);
                lastResult = Read(shared->lastResult);
            }
            if (lastResult != previous && FAILED(lastResult))
                Log(L"XAML controller result=" + std::to_wstring(lastResult));
        }
        else if (backend == Legacy) {
            if (!setComposition) { lastResult = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND); return; }
            // Undocumented WCA_ACCENT_POLICY (19), ACCENT_ENABLE_ACRYLICBLURBEHIND (4).
            // Legacy tint uses an ABGR alpha overlay for theme contrast.
            AccentPolicy policy{4, 0, Recipe(mode, SystemLightTheme()).legacyColor, 0};
            const CompositionData data{19, &policy, sizeof(policy)};
            LONG result = S_OK;
            for (const HWND taskbar : Taskbars()) {
                SetLastError(ERROR_SUCCESS);
                if (setComposition(taskbar, &data)) {
                    ++applied;
                    if (std::find(changedLegacyTaskbars.begin(), changedLegacyTaskbars.end(), taskbar) == changedLegacyTaskbars.end())
                        changedLegacyTaskbars.push_back(taskbar);
                } else {
                    const auto error = GetLastError();
                    result = error ? HRESULT_FROM_WIN32(error) : E_FAIL;
                }
            }
            if (result != lastResult) Log(L"Accent result=" + std::to_wstring(result));
            lastResult = result;
        }
    }
    void CycleAppearance() {
        mode = NextAcrylicMode(mode);
        Log(AcrylicLabel(mode));
        Refresh();
    }
    void UpdateAction() {
        const auto available = updates.Result();
        if (UpdateBusy(available.status)) return;
        if (available.status == UpdateStatus::Available) {
            updates.Download();
        } else updates.Start();
        SetTimer(window, UpdateTimer, 150, nullptr);
    }
    Handle updateInstaller;
    std::wstring updateDownload;
    void PollUpdate() {
        if (updateInstaller) {
            if (WaitForSingleObject(updateInstaller.get(), 0) != WAIT_OBJECT_0) return;
            DWORD error = ERROR_INSTALL_FAILURE;
            GetExitCodeProcess(updateInstaller.get(), &error);
            updateInstaller.reset();
            RemoveUpdateDownload(updateDownload); updateDownload.clear();
            // A successful installer replaces this controller. If it exited
            // while we are still here, keep the working app and allow retry.
            updates.DownloadFailed(error ? error : ERROR_INSTALL_FAILURE);
            Log(L"Update installation failed=" + std::to_wstring(error));
            KillTimer(window, UpdateTimer);
            RefreshUpdateMenu(window);
            return;
        }
        UpdateResult result;
        if (!updates.Poll(result)) return;
        if (result.status == UpdateStatus::Ready) {
            DWORD error = VerifyUpdateFile(result, result.installer);
            if (!error) {
                std::wstring command = L"\"" + result.installer + L"\" --update";
                STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
                startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
                if (CreateProcessW(result.installer.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                    std::filesystem::path(result.installer).parent_path().c_str(), &startup, &process)) {
                    CloseHandle(process.hThread); updateInstaller.reset(process.hProcess);
                    updateDownload = result.installer;
                    updates.Installing(); RefreshUpdateMenu(window); return;
                }
                error = GetLastError();
            }
            RemoveUpdateDownload(result.installer);
            updates.DownloadFailed(error);
        }
        KillTimer(window, UpdateTimer);
        RefreshUpdateMenu(window);
        Log(L"Update check status=" + std::to_wstring(static_cast<UINT>(result.status)) + L" error=" + std::to_wstring(result.error));
    }
    UINT ChangeDate() {
        const DWORD error = SaveClockDate(!clock.date);
        if (error) { Log(L"Date setting error=" + std::to_wstring(error)); tray.DateError(error); }
        else {
            clock.date = !clock.date;
            Log(std::wstring(L"Clock date=") + (clock.date ? L"on" : L"off"));
            Refresh();
            SetBridgeMode(shared, explorerPid, mode, true);
        }
        return clock.Packed();
    }
    ~Controller() {
        ReleaseBridge();
        RestoreLegacy();
        if (foregroundHook) UnhookWinEvent(foregroundHook);
        if (powerNotification) UnregisterPowerSettingNotification(powerNotification);
        if (bridgeDll) FreeLibrary(bridgeDll);
    }
};
Controller* controller = nullptr;
void CALLBACK ForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (controller && controller->window && controller->backend == Legacy)
        SetTimer(controller->window, DebounceTimer, 200, nullptr);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Controller*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Controller*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window = window;
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == QueryMessage) {
        if (wParam == WallpaperStatusQuery) return self->wallpaper ? static_cast<UINT>(self->wallpaper->Status()) : 0;
        if (wParam == WallpaperResultQuery) return self->wallpaper ? self->wallpaper->Error() : S_OK;
        if (wParam == StartCountQuery) return self->start.Count();
        if (wParam == StartAttachedQuery) return self->start.Attached();
        if (wParam == StartResultQuery) return self->start.Result();
        if (wParam == StartSearchQuery) return self->start.SearchCount();
        if (wParam == StartSpacingQuery) return self->start.SpacingCount();
        if (wParam == SearchAttachedQuery) return self->search.Attached();
        if (wParam == SearchInputQuery) return self->search.SearchCount();
        if (wParam == SearchResultQuery) return self->search.Result();
        if (wParam == BackendQuery) return self->backend;
        if (wParam == AppliedQuery) return self->shared ? Read(self->shared->appliedCount) : self->applied;
        if (wParam == ResultQuery) return self->lastResult != S_OK ? self->lastResult
            : self->shared ? Read(self->shared->lastResult) : S_OK;
        if (wParam == BridgeQuery) return self->shared ? Read(self->shared->bridgeState) : Idle;
        if (wParam == EnabledQuery) return self->mode != AcrylicMode::Off;
        if (wParam == AcrylicModeQuery) return static_cast<LONG>(self->mode);
        if (wParam == ClockQuery) return self->shared ? Read(self->shared->clockCount) : 0;
        if (wParam == AdaptiveGlyphQuery) return self->shared ? Read(self->shared->adaptiveGlyphs) : 0;
        if (wParam == AdaptiveBitmapQuery) return self->shared ? Read(self->shared->adaptiveBitmaps) : 0;
        if (wParam == AdaptiveResultQuery) return self->shared ? Read(self->shared->adaptiveResult) : S_OK;
        if (wParam == ClockResultQuery) return self->shared ? Read(self->shared->clockResult) : S_OK;
        if (wParam == VolumeQuery) return self->shared ? Read(self->shared->volumeCount) : 0;
        if (wParam == VolumeResultQuery) return self->shared ? Read(self->shared->volumeResult) : S_OK;
        if (wParam == IslandsQuery) return self->observedIslands;
        if (wParam == RetainedQuery) return self->bridgeRetained;
        if (wParam == ClockDateQuery) return self->clock.date;
        if (wParam == TrayQuery) return self->tray.Present();
        if (wParam == CursorModeQuery) return self->cursors->Mode();
        if (wParam == CursorAppliedQuery) return self->cursors->Applied();
        if (wParam == CursorErrorQuery) return self->cursors->Error();
        if (wParam == BorderQuery) return self->shared ? Read(self->shared->borderCount) : 0;
        if (wParam == LightQuery) return SystemLightTheme();
    } else if (message == TrayMessage) {
        const UINT event = LOWORD(lParam);
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_CONTEXTMENU) {
            self->cursors->Refresh();
            const UINT selected = self->tray.Menu(self->mode, self->cursors->Mode());
            if (selected == TrayExitCommand) PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    } else if (message == TrayThemeQueryMessage) {
        return wParam ? static_cast<UINT>(self->autoTheme->Status()) : SystemLightTheme();
    } else if (message == TrayDateQueryMessage) {
        return self->clock.Packed();
    } else if (message == TrayTaskbarQueryMessage) {
        return ReadTaskbarOptions().Packed();
    } else if (message == TrayWallpaperQueryMessage) {
        if(wParam==2)return static_cast<UINT>(self->wallpaperTheme);
        return self->wallpaper ? (wParam ? self->wallpaper->Error() : static_cast<LRESULT>(self->wallpaper->Status())) : 0;
    } else if (message == TrayUpdateQueryMessage) {
        return static_cast<UINT>(self->updates.Status());
    } else if (message == TrayCursorQueryMessage) {
        return wParam == 1 ? self->cursors->Mode() : self->cursors->Available();
    } else if (message == TrayCursorPreviewMessage) {
        return wParam==CursorPreviewNone?0:reinterpret_cast<LRESULT>(self->cursors->LoadPreview(static_cast<UINT>(wParam)));
    } else if (message == TrayCursorIconMessage) {
        return reinterpret_cast<LRESULT>(self->cursors->MenuIcon(static_cast<int>(wParam)));
    } else if (message == TraySettingMessage) {
        if(wParam==TrayAutoHideCommand){const auto result=ToggleTaskbarAutoHide();if(result.error)Log(L"Taskbar auto hide error="+std::to_wstring(result.error));RefreshTaskbarMenu(window);return result.Packed();}
        if(wParam==TrayWallpaperThemeCommand)return self->ChangeWallpaperTheme();
        if (wParam == TrayWallpaperCommand) {
            self->RefreshWallpaperTheme();
            if (self->wallpaper) self->wallpaper->Toggle(self->wallpaperLight);
            self->RefreshWallpaperTheme(false,true);
            return self->wallpaper ? static_cast<UINT>(self->wallpaper->Status()) : 0;
        }
        if (wParam == TrayGithubCommand) {
            return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", GithubUrl, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
        }
        if (wParam == TrayCursorUnlockCommand) {
            const DWORD error = OpenCursorGuide();
            if (error) self->tray.UpdateNotice(L"cursor guide could not be opened", L"The cursor files folder could not be created or opened. Check your account's folder permissions.", true);
            return self->cursors->Mode();
        }
        if (wParam == TrayUpdateCommand) {
            self->UpdateAction();
            return static_cast<UINT>(self->updates.Status());
        }
        if (wParam == TrayDateCommand) return self->ChangeDate();
        if (wParam == TrayThemeCommand) {
            const auto state = self->autoTheme->Status();
            if (state == AutoThemeStatus::Manual && !SystemLightTheme()) {
                self->autoTheme->Enable();
                self->RefreshAutoTheme();
                self->Refresh();
                return SystemLightTheme();
            }
            const DWORD disabled = self->autoTheme->Disable();
            self->RefreshAutoTheme();
            const auto change = disabled ? ThemeChangeResult{disabled} : SetWindowsLightTheme(state != AutoThemeStatus::Manual ? true : !SystemLightTheme());
            if (change.error) {
                Log(L"Windows mode error=" + std::to_wstring(change.error) + L" rollback=" + std::to_wstring(change.rollbackError));
                self->tray.ThemeError(change.error, change.rollbackError != 0);
            } else Log(SystemLightTheme() ? L"Windows light mode selected" : L"Windows dark mode selected");
            if (change.notificationError) self->tray.ThemeError(change.notificationError, false, true);
            self->RefreshWallpaperTheme();
            self->Refresh();
            SetBridgeMode(self->shared, self->explorerPid, self->mode, true);
            return SystemLightTheme();
        }
        if (wParam == TrayToggleCommand) {
            self->CycleAppearance();
            return static_cast<LONG>(self->mode);
        }
        if (wParam >= CursorCommandBase && wParam <= CursorCommandBase + CursorDisabled) {
            const DWORD error = self->cursors->Select(static_cast<UINT>(wParam) - CursorCommandBase);
            if (error) { Log(L"Cursor selection error=" + std::to_wstring(error)); self->tray.CursorError(error, self->cursors->SaveFailed()); }
        }
        return self->cursors->Mode();
    } else if (message == SearchIdentityReadyMessage) {
        self->Refresh();
        return 0;
    } else if (message == AutoThemeChangedMessage || message == WallpaperAutoThemeChangedMessage || message == WM_TIMECHANGE) {
        if(message!=WallpaperAutoThemeChangedMessage)self->RefreshAutoTheme(message == WM_TIMECHANGE);
        self->RefreshWallpaperTheme(message == WM_TIMECHANGE);
        self->Refresh();
        return 0;
    } else if (message == WallpaperChangedMessage) {
        if (self->wallpaper && FAILED(self->wallpaper->Error()))
            Log(L"Wallpaper error=" + std::to_wstring(self->wallpaper->Error())+L" stage="+self->wallpaper->ErrorStage());
        self->RefreshWallpaperTheme();
        RefreshWallpaperMenu(window);
        return 0;
    } else if (message == WM_TIMER) {
        if (wParam == WallpaperAutoThemeTimer) { self->RefreshWallpaperTheme(); return 0; }
        if (wParam == AutoThemeTimer) { self->RefreshAutoTheme(); self->Refresh(); return 0; }
        if (wParam == UpdateTimer) { self->PollUpdate(); return 0; }
        if (wParam == FastAttachTimer) { self->FastAttach(); return 0; }
        if (wParam == DebounceTimer) KillTimer(window, DebounceTimer);
        self->Refresh();
        return 0;
    } else if (message == self->taskbarCreated || message == WM_DISPLAYCHANGE
        || message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_DWMCOMPOSITIONCHANGED
        || message == WM_DPICHANGED) {
        if (message == WM_SETTINGCHANGE) self->RefreshAutoTheme();
        if (self->wallpaper && (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_DISPLAYCHANGE))
            self->RefreshWallpaperTheme(false,false,message==WM_SETTINGCHANGE || message==WM_DISPLAYCHANGE);
        RefreshTaskbarMenu(window);
        if (message == self->taskbarCreated) {
            self->tray.ShellRestarted();
            self->attachStarted = GetTickCount64();
            self->Refresh();
            SetTimer(window, FastAttachTimer, 100, nullptr);
            return 0;
        }
        SetTimer(window, DebounceTimer, 300, nullptr);
        return 0;
    } else if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND){
            self->RefreshAutoTheme(true);self->RefreshWallpaperTheme(true);
        }
        if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(setting->PowerSetting, PowerSavingGuid) && setting->DataLength == sizeof(DWORD)) {
                DWORD status = 0;
                CopyMemory(&status, setting->Data, sizeof(status));
                Log(L"Battery saver=" + std::to_wstring(status) + L"; selected appearance remains unchanged");
            }
        }
        SetTimer(window, DebounceTimer, 500, nullptr);
        return TRUE; // Neither quitting nor selecting a different appearance on battery saver.
    } else if (message == WM_QUERYENDSESSION) return TRUE;
    else if (message == WM_CLOSE || (message == WM_ENDSESSION && wParam)) {
        DestroyWindow(window);
        return 0;
    } else if (message == WM_DESTROY) {
        self->wallpaperAutoTheme.reset();
        self->wallpaper.reset();
        self->autoTheme.reset();
        self->tray.Remove();
        self->window = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return static_cast<int>(GetLastError());
    DWORD commandResult = ERROR_SUCCESS;
    if (argc > 1) {
        if (argc == 2 && wcscmp(argv[1], L"--install") == 0) commandResult = Startup(true);
        else if (argc == 2 && wcscmp(argv[1], L"--uninstall") == 0) {
            commandResult = Startup(false);
            if (commandResult == ERROR_SUCCESS) commandResult = Stop();
        } else if (argc == 2 && wcscmp(argv[1], L"--stop") == 0) commandResult = Stop();
        else if (argc == 3 && wcscmp(argv[1], L"--cursor-pack") == 0) commandResult = CursorManager::ConfigurePack(argv[2]);
        else if (argc == 3 && wcscmp(argv[1], L"--verify-cursors") == 0) commandResult = CursorManager::VerifyPack(argv[2]);
        else if (argc == 3 && wcscmp(argv[1], L"--import-cursors") == 0) {
            std::wstring installed;
            commandResult = ImportCursorArchive(argv[2], CursorDataFolder(), installed);
        }
        else if (argc == 2 && wcscmp(argv[1], L"--restore-cursors") == 0)
            commandResult = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0) ? ERROR_SUCCESS : GetLastError();
        else if (argc == 3 && wcscmp(argv[1], L"--diagnose") == 0) commandResult = Diagnose(argv[2]);
        else commandResult = ERROR_INVALID_PARAMETER;
        LocalFree(argv);
        return static_cast<int>(commandResult);
    }
    LocalFree(argv);
    Handle singleton(CreateMutexW(nullptr, FALSE, L"Local\\AcrylicTaskbar.Controller.v1"));
    if (!singleton) return static_cast<int>(GetLastError());
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    Controller app;
    // Apply before download housekeeping, tray artwork, or cursor refresh.
    app.RefreshTaskbar();
    app.cursors = std::make_unique<CursorManager>();
    CleanupUpdateDownloads();
    DWORD updateError = 0, updateBytes = sizeof(updateError);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", L"UpdateError", RRF_RT_REG_DWORD,
        nullptr, &updateError, &updateBytes) == ERROR_SUCCESS && updateError) app.updates.DownloadFailed(updateError);
    controller = &app;
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = WindowClass;
    if (!RegisterClassW(&wc)) return static_cast<int>(GetLastError());
    // A hidden top-level window receives shell/system broadcasts; HWND_MESSAGE would not.
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WindowClass, L"jellyflower",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, &app);
    if (!window) return static_cast<int>(GetLastError());
    app.autoTheme = std::make_unique<AutoTheme>(window);
    app.wallpaper = std::make_unique<WallpaperManager>(window, SystemLightTheme());
    app.powerNotification = RegisterPowerSettingNotification(window, &PowerSavingGuid, DEVICE_NOTIFY_WINDOW_HANDLE);
    app.foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
        ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!SetTimer(window, RefreshTimer, RefreshMs, nullptr)) {
        const auto error = GetLastError();
        DestroyWindow(window);
        return static_cast<int>(error ? error : ERROR_NOT_ENOUGH_MEMORY);
    }
    Log(L"Started pid=" + std::to_wstring(GetCurrentProcessId()));
    app.RefreshAutoTheme();
    app.RefreshWallpaperTheme();
    app.Refresh();
    SetTimer(window, FastAttachTimer, 100, nullptr);
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    controller = nullptr;
    Log(L"Stopped");
    return result == -1 ? static_cast<int>(GetLastError()) : 0;
}

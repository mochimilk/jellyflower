// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <string>
#include <vector>
#include "shared.hpp"
#include "version.hpp"
#include "resource.h"

using namespace acrylic;
namespace fs = std::filesystem;
namespace {
constexpr wchar_t UninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\jellyflower";
struct Payload { int id; const wchar_t* name; };
constexpr Payload Files[] = {{201, L"jellyflower.exe"}, {202, L"jellyflower-bridge.dll"},
    {203, L"LICENSE.md"}, {204, L"THIRD_PARTY_NOTICES.md"}, {205, L"LICENSE-LUCIDE.txt"},
    {206, L"README.md"}, {207, L"app-dark.ico"}, {208, L"app-light.ico"}};
std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, 0, nullptr, &value);
    if (FAILED(result)) throw static_cast<DWORD>(HRESULT_CODE(result));
    std::wstring path(value); CoTaskMemFree(value); return path;
}
fs::path Self() {
    wchar_t path[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!size || size == std::size(path)) throw static_cast<DWORD>(ERROR_BAD_PATHNAME);
    return path;
}
bool NativeArchitecture() {
    SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
#ifdef _M_ARM64
    return system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
#else
    return system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
#endif
}
bool Light() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
}
void Put(HKEY key, const wchar_t* name, const std::wstring& value) {
    const LSTATUS result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (result) throw static_cast<DWORD>(result);
}
void Extract(const fs::path& destination) {
    fs::create_directories(destination);
    // Fixed resource names only: no archive paths, downloads, or external tools.
    for (const auto& entry : Files) {
        const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(entry.id), RT_RCDATA);
        if (!resource) throw GetLastError();
        const DWORD size = SizeofResource(nullptr, resource);
        const void* bytes = LockResource(LoadResource(nullptr, resource));
        if (!bytes || !size) throw static_cast<DWORD>(ERROR_RESOURCE_DATA_NOT_FOUND);
        Handle file(CreateFileW((destination / entry.name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) throw GetLastError();
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes, size, &written, nullptr) || written != size) throw GetLastError();
    }
}
DWORD Run(const fs::path& executable, const wchar_t* argument, bool wait = true) {
    std::wstring command = L"\"" + executable.wstring() + L"\" " + argument;
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, &process)) return GetLastError();
    Handle thread(process.hThread), handle(process.hProcess);
    if (!wait) return ERROR_SUCCESS;
    if (WaitForSingleObject(handle.get(), 15000) != WAIT_OBJECT_0) return ERROR_TIMEOUT;
    DWORD exit = ERROR_GEN_FAILURE; GetExitCodeProcess(handle.get(), &exit); return exit;
}
fs::path Shortcut() { return fs::path(Known(FOLDERID_Programs)) / L"jellyflower.lnk"; }
void Link(const fs::path& folder) {
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(result)) throw static_cast<DWORD>(HRESULT_CODE(result));
    IPersistFile* persistence = nullptr;
    result = link->SetPath((folder / L"jellyflower.exe").c_str());
    if (SUCCEEDED(result)) result = link->SetWorkingDirectory(folder.c_str());
    if (SUCCEEDED(result)) result = link->SetDescription(L"jellyflower taskbar utility");
    if (SUCCEEDED(result)) result = link->SetIconLocation((folder / (Light() ? L"app-light.ico" : L"app-dark.ico")).c_str(), 0);
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_PPV_ARGS(&persistence));
    const auto path = Shortcut();
    if (SUCCEEDED(result)) result = persistence->Save(path.c_str(), TRUE);
    if (persistence) persistence->Release(); link->Release();
    if (FAILED(result)) throw static_cast<DWORD>(HRESULT_CODE(result));
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, path.c_str(), nullptr);
}
void Register(const fs::path& folder) {
    HKEY key = nullptr;
    const LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, UninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result) throw static_cast<DWORD>(result);
    try {
        Put(key, L"DisplayName", L"jellyflower"); Put(key, L"DisplayVersion", AppVersion);
        Put(key, L"Publisher", L"mochimilk"); Put(key, L"InstallLocation", folder.wstring());
        Put(key, L"DisplayIcon", (folder / L"jellyflower.exe").wstring());
        Put(key, L"UninstallString", L"\"" + (folder / L"uninstall.exe").wstring() + L"\" --uninstall");
        Put(key, L"URLInfoAbout", L"https://github.com/mochimilk/jellyflower");
        Put(key, L"URLUpdateInfo", ReleasesUrl);
        const DWORD one = 1;
        RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    } catch (...) { RegCloseKey(key); throw; }
    RegCloseKey(key);
}
void RemoveKnownFiles(const fs::path& folder) noexcept {
    for (const auto& entry : Files) { std::error_code error; fs::remove(folder / entry.name, error); }
    std::error_code error; fs::remove(folder / L"uninstall.exe", error); fs::remove(folder, error);
}
void Uninstall() {
    const auto base = fs::path(Known(FOLDERID_LocalAppData)) / L"Programs" / L"jellyflower";
    const auto folder = Self().parent_path();
    // Never follow a supplied/computed arbitrary path when removing an installation.
    if (fs::weakly_canonical(folder).parent_path() != fs::weakly_canonical(base)) throw static_cast<DWORD>(ERROR_BAD_PATHNAME);
    const DWORD result = Run(folder / L"jellyflower.exe", L"--uninstall");
    if (result) throw result; // Refuse an obsolete uninstaller if a newer copy owns startup.
    const auto link = Shortcut();
    std::error_code ignored; fs::remove(link, ignored);
    SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, link.c_str(), nullptr);
    const LSTATUS removed = RegDeleteTreeW(HKEY_CURRENT_USER, UninstallKey);
    if (removed && removed != ERROR_FILE_NOT_FOUND) throw static_cast<DWORD>(removed);
    // All child paths must be real version directories directly below our exact base.
    for (const auto& entry : fs::directory_iterator(base)) {
        if (entry.is_directory() && !entry.is_symlink()
            && entry.path().filename().wstring().rfind(L"v", 0) == 0
            && fs::weakly_canonical(entry.path()).parent_path() == fs::weakly_canonical(base)) RemoveKnownFiles(entry.path());
    }
    fs::remove(base, ignored);
    MessageBoxW(nullptr, L"jellyflower has been removed from login startup and your taskbar has been restored.\n\nWindows may retain helper files until you sign out. You can then delete the jellyflower folder under LocalAppData\\Programs.",
        L"jellyflower", MB_OK | MB_ICONINFORMATION);
}
void Install() {
    if (!NativeArchitecture()) throw static_cast<DWORD>(ERROR_EXE_MACHINE_TYPE_MISMATCH);
    const auto base = fs::path(Known(FOLDERID_LocalAppData)) / L"Programs" / L"jellyflower";
    const auto folder = base / (std::wstring(L"v") + AppVersion + L"-" + Architecture + L"-" + std::to_wstring(GetTickCount64()));
    // Stage and verify resources before stopping the currently working copy.
    try { Extract(folder); fs::copy_file(Self(), folder / L"uninstall.exe"); }
    catch (...) { RemoveKnownFiles(folder); throw; }
    DWORD result = Run(folder / L"jellyflower.exe", L"--stop");
    if (result) { RemoveKnownFiles(folder); throw result; }
    result = Run(folder / L"jellyflower.exe", L"--install");
    if (result) { RemoveKnownFiles(folder); throw result; }
    Register(folder); Link(folder);
    DWORD explorer = 0;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &explorer);
    const bool reload = explorer && OlderBridgePresent(explorer);
    result = Run(folder / L"jellyflower.exe", L"", false);
    if (result) throw result;
    std::wstring message = L"jellyflower is installed and will start when you sign in.";
    if (reload) message += L"\n\nSign out and back in once to finish this update.";
    message += L"\n\nOpen its flower icon under show hidden icons to use the menu.\n\nQuit TranslucentTB if it is still running.";
    MessageBoxW(nullptr, message.c_str(), L"jellyflower", MB_OK | MB_ICONINFORMATION);
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int count = 0; PWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    DWORD error = ERROR_SUCCESS;
    Handle setupMutex(CreateMutexW(nullptr, FALSE, L"Local\\jellyflower.Setup.v1"));
    const DWORD mutexError = GetLastError();
    try {
        if (!setupMutex) throw mutexError;
        if (mutexError == ERROR_ALREADY_EXISTS) throw static_cast<DWORD>(ERROR_BUSY);
        if (!arguments) throw GetLastError();
        if (count == 1) Install();
        else if (count == 2 && wcscmp(arguments[1], L"--uninstall") == 0) Uninstall();
        else if (count == 3 && wcscmp(arguments[1], L"--extract") == 0) Extract(fs::absolute(arguments[2]));
        else throw static_cast<DWORD>(ERROR_INVALID_PARAMETER);
    } catch (DWORD value) { error = value ? value : ERROR_GEN_FAILURE; }
    catch (const fs::filesystem_error& value) { error = static_cast<DWORD>(value.code().value()); }
    catch (...) { error = ERROR_GEN_FAILURE; }
    if (arguments) LocalFree(arguments);
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (error) {
        const auto message = std::wstring(L"jellyflower could not finish. Windows error ") + std::to_wstring(error)
            + (error == ERROR_EXE_MACHINE_TYPE_MISMATCH ? L". Download the installer matching your Windows system type (x64 or ARM64)." : L". Close other setup windows and try again.");
        MessageBoxW(nullptr, message.c_str(), L"jellyflower", MB_OK | MB_ICONERROR);
    }
    return static_cast<int>(error);
}

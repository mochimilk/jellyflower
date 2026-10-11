// SPDX-License-Identifier: GPL-3.0-or-later
#include "cursors.hpp"
#include "cursor_icon.hpp"
#include "theme.hpp"
#include <shlobj.h>
#include <array>
#include <vector>
#include <utility>
#include <cwchar>

namespace acrylic {
namespace {
constexpr wchar_t SettingsKey[] = L"Software\\AcrylicTaskbar";
struct Role { DWORD id; const wchar_t* pack; const wchar_t* stock; };
// Standard Windows cursor resource IDs, including the newer person/pin roles.
constexpr std::array<Role, 17> Roles{{
    {32512, L"arrow.cur", L"aero_arrow.cur"}, {32651, L"help.cur", L"aero_helpsel.cur"},
    {32650, L"appstarting.ani", L"aero_working.ani"}, {32514, L"wait.ani", L"aero_busy.ani"},
    {32515, L"crosshair.cur", L"cross_r.cur"}, {32513, L"ibeam.cur", L"beam_r.cur"},
    {32631, L"nwpen.cur", L"aero_pen.cur"}, {32648, L"no.cur", L"aero_unavail.cur"},
    {32645, L"sizens.cur", L"aero_ns.cur"}, {32644, L"sizewe.cur", L"aero_ew.cur"},
    {32642, L"sizenwse.cur", L"aero_nwse.cur"}, {32643, L"sizenesw.cur", L"aero_nesw.cur"},
    {32646, L"sizeall.cur", L"aero_move.cur"}, {32516, L"uparrow.cur", L"aero_up.cur"},
    {32649, L"hand.cur", L"aero_link.cur"}, {32672, L"person.cur", L"aero_person.cur"},
    {32671, L"pin.cur", L"aero_pin.cur"}
}};
struct Loaded {
    std::array<HCURSOR, Roles.size()> handles{};
    ~Loaded() { for (auto cursor : handles) if (cursor) DestroyCursor(cursor); }
};
DWORD LoadSet(const std::wstring& folder, bool stock, Loaded& set) {
    for (size_t index = 0; index < Roles.size(); ++index) {
        const auto path = folder + L"\\" + (stock ? Roles[index].stock : Roles[index].pack);
        set.handles[index] = LoadCursorFromFileW(path.c_str());
        if (!set.handles[index]) { const auto error = GetLastError(); return error ? error : ERROR_INVALID_DATA; }
    }
    return ERROR_SUCCESS;
}
std::wstring ReadFolder(const std::wstring& importFolder) {
    const auto imported = ActiveCursorFolder(importFolder);
    if (!imported.empty()) return imported;
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"CursorPack", RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS
        && bytes > sizeof(wchar_t) && bytes < 65536) {
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1);
        if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"CursorPack", RRF_RT_REG_SZ, nullptr, value.data(), &bytes) == ERROR_SUCCESS)
            return value.data();
    }
    return {};
}
DWORD SaveMode(UINT mode) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return static_cast<DWORD>(result);
    const DWORD value = mode;
    result = RegSetValueExW(key, L"CursorMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return static_cast<DWORD>(result);
}
} // namespace

const wchar_t* CursorManager::Name(UINT mode) {
    if (mode == CursorDefault) return L"default";
    if (mode == CursorDisabled) return L"disable";
    return mode <= CursorColors.size() ? CursorColors[mode - 1].label : L"unknown";
}
HICON CursorManager::MenuIcon(int pixels) const {
    std::wstring path;
    if (mode_ == CursorDisabled) {
        wchar_t windows[MAX_PATH]{};
        if (GetWindowsDirectoryW(windows, MAX_PATH)) path = std::wstring(windows) + L"\\Cursors\\aero_arrow.cur";
    } else {
        const UINT color = mode_ == CursorDefault ? (SystemLightTheme() ? 1 : 2) : mode_;
        path = folder_ + L"\\cursor\\assets\\" + CursorColors[color - 1].folder + L"\\arrow.cur";
    }
    return CreateCursorMenuIcon(path.c_str(), pixels);
}
CursorManager::CursorManager(std::wstring importFolder) : folder_(ReadFolder(importFolder)), importer_(std::move(importFolder)) {
    available_ = !folder_.empty() && VerifyPack(folder_) == ERROR_SUCCESS;
    DWORD mode = CursorDefault, bytes = sizeof(mode);
    const LONG saved = RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"CursorMode", RRF_RT_REG_DWORD, nullptr, &mode, &bytes);
    if (saved == ERROR_SUCCESS && mode <= CursorDisabled && (Available() & (1u << mode))) mode_ = mode;
    else {
        // Fresh installs keep Windows' configured cursor until a custom choice.
        mode_ = CursorDisabled;
        started_ = true;
    }
}
CursorManager::~CursorManager() {
    // This app does not replace the user's stored Windows cursor scheme.
    if (touched_) SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
}
DWORD CursorManager::VerifyPack(const std::wstring& folder) {
    return VerifyCursorFolder(folder);
}
DWORD CursorManager::ConfigurePack(const wchar_t* folder) {
    if (!folder || !*folder) return ERROR_BAD_PATHNAME;
    const DWORD error = VerifyPack(folder);
    if (error) return error;
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return static_cast<DWORD>(result);
    result = RegSetValueExW(key, L"CursorPack", 0, REG_SZ, reinterpret_cast<const BYTE*>(folder),
        static_cast<DWORD>((wcslen(folder) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return static_cast<DWORD>(result);
}
DWORD CursorManager::Apply(UINT mode, bool light) {
    Loaded set;
    std::wstring path;
    const bool stock = mode == CursorDisabled;
    if (stock) {
        wchar_t windows[MAX_PATH]{};
        if (!GetWindowsDirectoryW(windows, MAX_PATH)) return GetLastError();
        path = std::wstring(windows) + L"\\Cursors";
    } else {
        const UINT color = mode == CursorDefault ? (light ? 1 : 2) : mode;
        path = folder_ + L"\\cursor\\assets\\" + CursorColors[color - 1].folder;
    }
    const DWORD loaded = LoadSet(path, stock, set);
    if (loaded) return loaded; // Validate the entire set before replacing any role.
    for (size_t index = 0; index < Roles.size(); ++index) {
        // SetSystemCursor consumes/destroys this private handle. Never pass a
        // shared LoadCursor handle, and never destroy it a second time.
        const HCURSOR cursor = std::exchange(set.handles[index], nullptr);
        if (!SetSystemCursor(cursor, Roles[index].id)) {
            const DWORD error = GetLastError();
            SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
            applied_ = false;
            return error ? error : ERROR_GEN_FAILURE;
        }
        touched_ = true;
    }
    applied_ = true;
    light_ = light;
    return ERROR_SUCCESS;
}
void CursorManager::Refresh() {
    CursorImportResult imported;
    if (importer_.Poll(imported)) {
        importNotice_ = true; importError_ = imported.error;
        if (!imported.error) {
            folder_ = std::move(imported.folder); available_ = true;
            if (mode_ != CursorDisabled) started_ = false;
        }
    }
    importer_.Scan();
    const bool light = SystemLightTheme();
    if (previewing_) {
        if (previewMode_ == CursorDefault && light != light_) Preview(previewMode_);
        return;
    }
    if (!started_ || (mode_ == CursorDefault && light != light_)) {
        saveFailed_ = false;
        error_ = Apply(mode_, light);
        started_ = true;
        // Failed auto application is retried only after the theme changes or a
        // new selection; a missing pack must not load files every five seconds.
        light_ = light;
    }
}
DWORD CursorManager::Select(UINT mode) {
    if (mode > CursorDisabled) { EndPreview(); return ERROR_INVALID_PARAMETER; }
    if (!(Available() & (1u << mode))) { EndPreview(); return ERROR_FILE_NOT_FOUND; }
    const bool preview = previewing_;
    const bool alreadyApplied = preview && previewMode_ == mode && applied_
        && (mode != CursorDefault || light_ == SystemLightTheme());
    previewing_ = false;
    saveFailed_ = false;
    error_ = alreadyApplied ? ERROR_SUCCESS : Apply(mode, SystemLightTheme());
    if (error_) { if (preview) RestoreSelection(); return error_; }
    mode_ = mode;
    started_ = true;
    error_ = SaveMode(mode);
    saveFailed_ = error_ != ERROR_SUCCESS;
    return error_;
}
DWORD CursorManager::RestoreSelection() {
    if (previewWindows_) {
        // A fresh install may use a Windows accessibility/custom scheme.
        // Return to that configured scheme rather than forcing aero_arrow.
        const bool restored = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0) != FALSE;
        applied_ = false;
        return restored ? ERROR_SUCCESS : GetLastError();
    }
    return Apply(mode_, SystemLightTheme());
}
DWORD CursorManager::Preview(UINT mode) {
    if (mode > CursorDisabled) return ERROR_INVALID_PARAMETER;
    if (!(Available() & (1u << mode))) return ERROR_FILE_NOT_FOUND;
    const bool light = SystemLightTheme();
    if (previewing_ && previewMode_ == mode && (mode != CursorDefault || light_ == light)) return ERROR_SUCCESS;
    if (!previewing_) previewWindows_ = mode_ == CursorDisabled && !applied_;
    const bool same = !previewing_ && mode == mode_ && started_ && (applied_ || previewWindows_)
        && (mode != CursorDefault || light_ == light);
    previewing_ = true; previewMode_ = mode;
    const DWORD error = same ? ERROR_SUCCESS : mode == CursorDisabled && previewWindows_
        ? RestoreSelection() : Apply(mode, light);
    if (error) { previewing_ = false; RestoreSelection(); }
    return error;
}
DWORD CursorManager::EndPreview() {
    if (!previewing_) return ERROR_SUCCESS;
    previewing_ = false;
    if (!previewWindows_ && previewMode_ == mode_ && (mode_ != CursorDefault || light_ == SystemLightTheme())) return ERROR_SUCCESS;
    const DWORD error = RestoreSelection();
    if (error) error_ = error;
    return error;
}
bool CursorManager::TakeImportResult(DWORD& error) {
    if (!importNotice_) return false;
    importNotice_ = false; error = importError_; return true;
}
} // namespace acrylic

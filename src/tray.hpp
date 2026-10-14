// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "appearance.hpp"

namespace acrylic {
inline constexpr UINT TrayMessage = WM_APP + 43;
// Apply a menu setting immediately; return its resulting state.
inline constexpr UINT TraySettingMessage = WM_APP + 44;
// Return a private cursor-outline HICON; the menu owns and destroys it.
inline constexpr UINT TrayCursorIconMessage = WM_APP + 45;
inline constexpr UINT TrayThemeQueryMessage = WM_APP + 46;
inline constexpr UINT TrayToggleCommand = 1, TrayExitCommand = 2;
inline constexpr UINT TrayCursorCommand = 3, CursorCommandBase = 100;
inline constexpr UINT TrayThemeCommand = 4;
inline constexpr UINT TrayDateCommand = 6;
inline constexpr UINT TrayDateQueryMessage = WM_APP + 47;
inline constexpr UINT TrayUpdateQueryMessage = WM_APP + 48;
inline constexpr UINT TrayUpdateCommand = 8;
inline constexpr UINT TrayGeneralCommand = 10;
inline constexpr UINT TrayGithubCommand = 11;
inline constexpr UINT TrayWallpaperCommand = 12, TrayWallpaperQueryMessage = WM_APP + 51;
inline constexpr UINT TrayTaskbarCommand = 13, TrayAutoHideCommand = 14;
inline constexpr UINT TrayWallpaperMenuCommand = 15, TrayWallpaperThemeCommand = 16;
inline constexpr UINT TrayTaskbarQueryMessage = WM_APP + 52;
inline constexpr wchar_t GithubUrl[] = L"https://github.com/mochimilk/jellyflower";
inline constexpr UINT TrayCursorUnlockCommand = 9, TrayCursorQueryMessage = WM_APP + 49;
inline constexpr UINT TrayCursorPreviewMessage = WM_APP + 50;
inline constexpr UINT CursorPreviewNone = ~0u;

class TrayIcon {
public:
    ~TrayIcon();
    void Refresh(HWND owner, AcrylicMode mode);
    void ShellRestarted() noexcept { present_ = false; }
    void Remove() noexcept;
    UINT Menu(AcrylicMode mode, UINT cursorMode) const;
    void CursorError(DWORD error, bool saveFailure = false) const;
    void ThemeError(DWORD error, bool partial = false, bool notification = false) const;
    void DateError(DWORD error) const;
    void UpdateNotice(const wchar_t* title, const wchar_t* body, bool error = false) const;
    bool Present() const noexcept { return present_; }
private:
    HWND window_ = nullptr;
    HICON icon_ = nullptr;
    bool present_ = false;
    int resource_ = 0, pixels_ = 0;
    int brandTheme_ = -1;
};
} // namespace acrylic

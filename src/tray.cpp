// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"
#include "popup.hpp"
#include "theme.hpp"
#include "resource.h"
#include "branding.hpp"
#include <shellapi.h>
#include <cwchar>

namespace acrylic {
namespace {
constexpr GUID TrayGuid = {0x529cd0f2, 0xa446, 0x47d3,
    {0xa9, 0x6e, 0xb7, 0x3e, 0x21, 0x40, 0xaa, 0x91}};
bool LightTaskbar() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        const COLORREF background = GetSysColor(COLOR_WINDOW);
        return GetRValue(background) + GetGValue(background) + GetBValue(background) > 384;
    }
    return SystemLightTheme();
}
NOTIFYICONDATAW Data(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = 1;
    data.guidItem = TrayGuid;
    data.uFlags = NIF_GUID;
    return data;
}
} // namespace

TrayIcon::~TrayIcon() {
    Remove();
    if (icon_) DestroyIcon(icon_);
}
void TrayIcon::Remove() noexcept {
    if (present_) {
        auto data = Data(window_);
        Shell_NotifyIconW(NIM_DELETE, &data);
        present_ = false;
    }
}
void TrayIcon::Refresh(HWND owner, AcrylicMode mode) {
    window_ = owner;
    const bool light = LightTaskbar();
    if (brandTheme_ != static_cast<int>(light)) {
        RefreshAppBranding(owner, light);
        brandTheme_ = static_cast<int>(light);
    }
    const int resource = light ? IDI_TRAY_LIGHT : IDI_TRAY_DARK;
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const UINT dpi = GetDpiForWindow(taskbar ? taskbar : owner);
    const int pixels = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96);
    if (!icon_ || resource != resource_ || pixels != pixels_) {
        const auto replacement = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(resource), IMAGE_ICON, pixels, pixels, 0));
        if (!replacement) return;
        if (icon_) DestroyIcon(icon_);
        icon_ = replacement;
        resource_ = resource;
        pixels_ = pixels;
    }
    auto data = Data(owner);
    data.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = icon_;
    swprintf_s(data.szTip, L"jellyflower - %ls (click for menu)", AcrylicLabel(mode));
    if (Shell_NotifyIconW(NIM_MODIFY, &data)) {
        present_ = true;
        return;
    }
    present_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (present_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) Remove();
    }
}
void TrayIcon::CursorError(DWORD error, bool saveFailure) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    wcscpy_s(data.szInfoTitle, saveFailure ? L"cursor preference could not be saved" : L"cursor could not be changed");
    swprintf_s(data.szInfo, error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
        ? L"Choose cursor > unlock colors for ZIP download and import instructions. Windows error: %lu."
        : L"Windows denied the operation or could not load this cursor. Use your normal Windows desktop session. Windows error: %lu.", error);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void TrayIcon::ThemeError(DWORD error, bool partial, bool notification) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    wcscpy_s(data.szInfoTitle, notification ? L"theme changed; apps could not be notified" : L"windows mode could not be changed");
    swprintf_s(data.szInfo, partial ? L"The system and app modes may differ. Check Windows Settings > Personalization > Colors. Windows error: %lu."
        : notification ? L"The preference was saved. Some apps may need to be reopened. Windows error: %lu."
        : L"Windows could not save the preference. Check Windows Settings > Personalization > Colors. Windows error: %lu.", error);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void TrayIcon::DateError(DWORD error) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO; data.dwInfoFlags = NIIF_WARNING;
    wcscpy_s(data.szInfoTitle, L"date setting could not be saved");
    swprintf_s(data.szInfo, L"Windows denied saving the date preference. Your previous choice is unchanged. Windows error: %lu.", error);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void TrayIcon::UpdateNotice(const wchar_t* title, const wchar_t* body, bool error) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO; data.dwInfoFlags = error ? NIIF_WARNING : NIIF_INFO;
    wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(data.szInfo, body, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
UINT TrayIcon::Menu(AcrylicMode mode, UINT cursorMode) const {
    POINT position{};
    NOTIFYICONIDENTIFIER identity{};
    identity.cbSize = sizeof(identity);
    identity.hWnd = window_;
    identity.uID = 1;
    identity.guidItem = TrayGuid;
    RECT bounds{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identity, &bounds))) {
        position = {bounds.left, bounds.top};
    } else GetCursorPos(&position);
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const UINT selected = ShowTrayMenu(window_, position,
        GetDpiForWindow(taskbar ? taskbar : window_), mode, LightTaskbar(), cursorMode, bounds);
    PostMessageW(window_, WM_NULL, 0, 0);
    auto data = Data(window_);
    Shell_NotifyIconW(NIM_SETFOCUS, &data);
    return selected;
}
} // namespace acrylic

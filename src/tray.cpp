// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"
#include "localization.hpp"
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
void TrayIcon::Refresh(HWND owner, AcrylicMode) {
    if(window_!=owner){Remove();brandTheme_=-1;}
    window_ = owner;
    const bool light = LightTaskbar();
    const auto language=GetLanguage();
    if (brandTheme_ != static_cast<int>(light) || brandLanguage_!=language) {
        RefreshAppBranding(owner, light);
        brandTheme_ = static_cast<int>(light);brandLanguage_=language;
    }
    const int resource = light ? IDI_TRAY_LIGHT : IDI_TRAY_DARK;
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const UINT dpi = GetDpiForWindow(taskbar ? taskbar : owner);
    const int pixels = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96);
    const bool artworkChanged=!icon_ || resource != resource_ || pixels != pixels_;
    if (artworkChanged) {
        const auto replacement = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(resource), IMAGE_ICON, pixels, pixels, 0));
        if (!replacement) return;
        if (icon_) DestroyIcon(icon_);
        icon_ = replacement;
        resource_ = resource;
        pixels_ = pixels;
    }
    // Re-sending NIF_ICON replaces Explorer's bitmap even when the HICON is
    // unchanged. Keep routine foreground/recovery refreshes presentation-free.
    // TaskbarCreated explicitly clears present_ through ShellRestarted().
    if(present_ && !artworkChanged && tipLanguage_==language)return;
    if(present_ && !artworkChanged){
        auto tip=Data(owner);tip.uFlags|=NIF_TIP|NIF_SHOWTIP;
        wcsncpy_s(tip.szTip,Tr(TextId::TrayTip),_TRUNCATE);
        if(Shell_NotifyIconW(NIM_MODIFY,&tip)){tipLanguage_=language;return;}
        present_=false;
    }
    auto data = Data(owner);
    data.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip,Tr(TextId::TrayTip),_TRUNCATE);
    tipLanguage_=language;
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
    wcsncpy_s(data.szInfoTitle,Tr(saveFailure?TextId::CursorSaveError:TextId::CursorChangeError),_TRUNCATE);
    const auto body=Text(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND?TextId::CursorMissingBody:TextId::CursorDeniedBody,{{L"error",std::to_wstring(error)}});
    wcsncpy_s(data.szInfo,body.c_str(),_TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void TrayIcon::ThemeError(DWORD error, bool partial, bool notification) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    wcsncpy_s(data.szInfoTitle,Tr(notification?TextId::ThemeNotifyError:TextId::ThemeChangeError),_TRUNCATE);
    const auto body=Text(partial?TextId::ThemePartialBody:notification?TextId::ThemeNotifyBody:TextId::ThemeSaveBody,{{L"error",std::to_wstring(error)}});
    wcsncpy_s(data.szInfo,body.c_str(),_TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void TrayIcon::DateError(DWORD error) const {
    auto data = Data(window_);
    data.uFlags |= NIF_INFO; data.dwInfoFlags = NIIF_WARNING;
    wcsncpy_s(data.szInfoTitle,Tr(TextId::DateSaveError),_TRUNCATE);
    const auto body=Text(TextId::DateSaveBody,{{L"error",std::to_wstring(error)}});
    wcsncpy_s(data.szInfo,body.c_str(),_TRUNCATE);
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

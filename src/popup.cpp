// SPDX-License-Identifier: GPL-3.0-or-later
#include "popup.hpp"
#include "tray.hpp"
#include "cursors.hpp"
#include "resource.h"
#include "updates.hpp"
#include "version.hpp"
#include "menu_surface.hpp"
#include "menu_style.hpp"
#include "menu_material.hpp"
#include "menu_text.hpp"
#include "auto_theme.hpp"
#include "cursor_icon.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <objidl.h>
#include <gdiplus.h>

namespace acrylic {
namespace {
constexpr wchar_t PopupClass[] = L"AcrylicTaskbar.TrayMenu.v1";
constexpr int Padding = 6, SeparatorHeight = 9;
constexpr int MenuIconSize = 20, MenuIconInset = 8;
// Keep a 20-DIP square slot, with a smaller glyph centered inside it. At 150%
// scaling the visible Lucide artwork is approximately 20 physical pixels.
constexpr int MenuGlyphSize = 16;
constexpr UINT_PTR HoverTimer = 1;
constexpr UINT_PTR ChildCloseTimer = 2, TrayDeactivateTimer = 3;
constexpr UINT_PTR UpdateSpinnerTimer = 4;
constexpr UINT_PTR ColorPreviewTimer = 5;
constexpr UINT_PTR EntranceTimer = 6;
// Documented Windows 11 DWM values; compatible with Windows 10 SDK headers.
constexpr auto CornerPreference = static_cast<DWMWINDOWATTRIBUTE>(33);
constexpr auto ImmersiveDarkMode = static_cast<DWMWINDOWATTRIBUTE>(20);
constexpr auto BorderColor = static_cast<DWMWINDOWATTRIBUTE>(34);
constexpr DWORD RoundCorners = 2;
struct Row { UINT command; std::wstring label; bool divider = false, checked = false; std::wstring secondary; bool enabled = true; };
std::vector<Row> CursorRows(UINT mask, UINT selected) {
    std::vector<Row> rows{{CursorCommandBase, L"default (system)", false, selected == CursorDefault, L"", (mask & 1) != 0}};
    for (size_t i = 0; i < CursorColors.size(); ++i)
        rows.push_back({CursorCommandBase + static_cast<UINT>(i) + 1, CursorColors[i].label, i == 0, selected == i + 1, L"", (mask & (1u << (i + 1))) != 0});
    rows.push_back({CursorCommandBase + CursorDisabled, L"disable", true, selected == CursorDisabled});
    return rows;
}
bool HasCursorPack(UINT mask) { return (mask & ((1u << CursorDisabled) - 1)) != 0; }
std::vector<Row> RootRows(AcrylicMode mode, bool date, bool light, UINT mask) {
    return {{TrayThemeCommand, L"system", false, false, light ? L"light" : L"dark"},
        {TrayToggleCommand, L"acrylic", false, false, AcrylicModeName(mode)},
        {TrayDateCommand, L"date", false, false, date ? L"on" : L"off"},
        HasCursorPack(mask) ? Row{TrayCursorCommand, L"cursor"} : Row{TrayCursorUnlockCommand, L"colors", false, false, L"unlock"},
        {TrayGeneralCommand, L"general", true}};
}
std::vector<Row> GeneralRows() {
    return {{TrayGithubCommand, L"github"}, {TrayUpdateCommand, std::wstring(L"v") + AppVersion}, {TrayExitCommand, L"exit", true}};
}
std::unique_ptr<Gdiplus::Bitmap> SpinnerBitmap(HICON icon) {
    // FromHICON loses the alpha channel on some Windows/GDI+ combinations.
    // Copy the icon's premultiplied 32-bit surface explicitly instead.
    ICONINFO source{};
    if (!GetIconInfo(icon, &source)) return {};
    BITMAP size{};
    const bool valid = source.hbmColor && GetObjectW(source.hbmColor, sizeof(size), &size)
        && size.bmWidth > 0 && size.bmHeight > 0 && size.bmBitsPixel == 32;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.bmWidth, -size.bmHeight, 1, 32};
    std::vector<BYTE> pixels(valid ? static_cast<size_t>(size.bmWidth) * size.bmHeight * 4 : 0);
    const HDC dc = GetDC(nullptr);
    const bool copied = valid && dc && GetDIBits(dc, source.hbmColor, 0, size.bmHeight, pixels.data(), &info, DIB_RGB_COLORS);
    if (dc) ReleaseDC(nullptr, dc);
    if (source.hbmColor) DeleteObject(source.hbmColor);
    if (source.hbmMask) DeleteObject(source.hbmMask);
    if (!copied) return {};
    auto result = std::make_unique<Gdiplus::Bitmap>(size.bmWidth, size.bmHeight, PixelFormat32bppPARGB);
    const Gdiplus::Rect bounds(0, 0, size.bmWidth, size.bmHeight);
    Gdiplus::BitmapData target{};
    if (result->LockBits(&bounds, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &target) != Gdiplus::Ok) return {};
    for (int y = 0; y < size.bmHeight; ++y)
        memcpy(static_cast<BYTE*>(target.Scan0) + y * target.Stride, pixels.data() + static_cast<size_t>(y) * size.bmWidth * 4, static_cast<size_t>(size.bmWidth) * 4);
    result->UnlockBits(&target);
    return result;
}
bool HasSubmenu(UINT command) { return command == TrayCursorCommand || command == TrayGeneralCommand; }
HICON LoadTintedIcon(int resource, int pixels, COLORREF ink) {
    const auto source = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_ICON, pixels, pixels, 0));
    ICONINFO info{};
    const bool read = source && GetIconInfo(source, &info);
    if (source) DestroyIcon(source);
    const HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO format{};
    format.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels, -pixels, 1, 32, BI_RGB};
    void* bits = nullptr;
    const HBITMAP bitmap = dc ? CreateDIBSection(dc, &format, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    HICON result = nullptr;
    if (read && info.hbmColor && bitmap && GetDIBits(dc, info.hbmColor, 0, pixels, bits, &format, DIB_RGB_COLORS) == pixels) {
        auto* rgba = static_cast<DWORD*>(bits);
        for (int i = 0; i < pixels * pixels; ++i) {
            const DWORD alpha = rgba[i] >> 24;
            auto channel = [alpha](BYTE value) { return (value * alpha + 127) / 255; };
            rgba[i] = (alpha << 24) | (channel(GetRValue(ink)) << 16) | (channel(GetGValue(ink)) << 8) | channel(GetBValue(ink));
        }
        ICONINFO tinted{TRUE, 0, 0, info.hbmMask, bitmap};
        result = CreateIconIndirect(&tinted);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}
// Cache both Windows contrast-theme states; normal themes use the same ink
// when hovered. Tint only the alpha mask, preserving the supplied SVG edges.
struct MenuIndicator {
    HICON icons[2]{};
    MenuIndicator() = default;
    MenuIndicator(const MenuIndicator&) = delete;
    MenuIndicator& operator=(const MenuIndicator&) = delete;
    ~MenuIndicator() { for (const auto icon : icons) if (icon) DestroyIcon(icon); }
    HICON Get(bool highlighted) const { return icons[highlighted ? 1 : 0]; }
    explicit operator bool() const { return icons[0] && icons[1]; }
    bool Load(int resource, int pixels, COLORREF normal, COLORREF highlighted) {
        HICON next[]{LoadTintedIcon(resource, pixels, normal), LoadTintedIcon(resource, pixels, highlighted)};
        if (!next[0] || !next[1]) {
            for (const auto icon : next) if (icon) DestroyIcon(icon);
            return false;
        }
        for (size_t i = 0; i < std::size(icons); ++i) {
            if (icons[i]) DestroyIcon(icons[i]);
            icons[i] = next[i];
        }
        return true;
    }
};
struct Popup;
Popup* activePopup = nullptr;
LRESULT CALLBACK PopupProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK PopupMouseProc(int, WPARAM, LPARAM);

struct Popup {
    HWND window = nullptr, controller = nullptr;
    std::vector<HWND> buttons;
    std::vector<Row> rows;
    std::unique_ptr<Popup> child;
    Popup* parent = nullptr;
    HFONT font = nullptr;
    MenuText menuText;
    UINT textFallbacks = 0;
    HHOOK mouseHook = nullptr;
    HICON acrylicIcons[3]{}, dateIcons[2]{}, cursorIcon = nullptr, themeIcon = nullptr, updateIcon = nullptr;
    HICON packageIcon = nullptr, packageOpenIcon = nullptr, generalIcon = nullptr;
    HICON githubIcon = nullptr;
    MenuIndicator checkIcon, chevronIcon, externalIcon;
    std::vector<HICON> colorIcons;
    size_t colorFrame = 0;
    bool hasCursorPack = false;
    ULONG_PTR graphicsToken = 0;
    std::unique_ptr<Gdiplus::Bitmap> spinnerBitmap;
    UpdateStatus updateStatus = UpdateStatus::Idle;
    ULONGLONG updateStarted = 0;
    HBRUSH background = nullptr, highlight = nullptr, separator = nullptr;
    COLORREF text = 0, highlightedText = 0, border = 0;
    UINT dpi = 96, result = 0, cursorMode = 0, hoverDelay = 400;
    RECT trayBounds{};
    int hovered = -1, childIndex = -1, suppressedHover = -1, rowHeight = 40;
    UINT kind = 0;
    UINT previewMode = CursorPreviewNone, previewAttempted = 0;
    std::array<HCURSOR,CursorDisabled+1> previewCursors{};
    HCURSOR previewCursor = nullptr;
    UINT iconCursorMode = CursorPreviewNone;int iconPixels = 0;bool iconLight = false;
    bool clockDate = true;
    AcrylicMode mode = AcrylicMode::System;
    bool light = false, systemLight = false, keyboard = false, done = false, closing = false;
    bool acrylic = false, highContrast = false;
    RECT restingBounds{};
    ULONGLONG entranceStarted = 0;
    bool entering = false;
    ~Popup() {
        PreviewCursor(-1);
        ClearPreviewCursors();
        if (mouseHook) UnhookWindowsHookEx(mouseHook);
        child.reset();
        if (window) DestroyWindow(window);
        if (font) DeleteObject(font);
        for (const auto icon : acrylicIcons) if (icon) DestroyIcon(icon);
        if (cursorIcon) DestroyIcon(cursorIcon);
        if (themeIcon) DestroyIcon(themeIcon);
        for (const auto icon : dateIcons) if (icon) DestroyIcon(icon);
        if (updateIcon) DestroyIcon(updateIcon);
        if (packageIcon) DestroyIcon(packageIcon);
        if (packageOpenIcon) DestroyIcon(packageOpenIcon);
        if (generalIcon) DestroyIcon(generalIcon);
        if (githubIcon) DestroyIcon(githubIcon);
        for (const auto icon : colorIcons) if (icon) DestroyIcon(icon);
        if (background) DeleteObject(background);
        if (highlight) DeleteObject(highlight);
        if (separator) DeleteObject(separator);
        spinnerBitmap.reset();
        if (graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken);
    }
    int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi), 96); }
    void CompleteEntrance() {
        if (!entering) return;
        entering = false;
        KillTimer(window, EntranceTimer);
        SetWindowPos(window, nullptr, restingBounds.left, restingBounds.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void AdvanceEntrance(ULONGLONG now) {
        if (!entering) return;
        const auto elapsed = now - entranceStarted;
        if (elapsed >= MenuEntranceDuration) { CompleteEntrance(); return; }
        const int offset = static_cast<int>(std::lround(Px(MenuEntranceDistance)
            * (1 - MenuEntranceProgress(static_cast<double>(elapsed) / MenuEntranceDuration))));
        SetWindowPos(window, nullptr, restingBounds.left, restingBounds.top + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void BeginEntrance() {
        if (parent || highContrast || !MenuAnimationsEnabled()) return;
        entranceStarted = GetTickCount64(); entering = true;
        AdvanceEntrance(entranceStarted);
        if (!SetTimer(window, EntranceTimer, 10, nullptr)) CompleteEntrance();
    }
    Popup* Root() { return parent ? parent->Root() : this; }
    Popup* Target(HWND target) {
        if (target == window || IsChild(window, target)) return this;
        return child ? child->Target(target) : nullptr;
    }
    void HideBranch() {
        PreviewCursor(-1);
        CompleteEntrance();
        closing = true;
        if (window) KillTimer(window, UpdateSpinnerTimer);
        if (child) child->HideBranch();
        if (window) ShowWindow(window, SW_HIDE);
    }
    bool ContainsPoint(POINT point) const {
        RECT bounds{};
        if (window && IsWindowVisible(window) && GetWindowRect(window, &bounds) && PtInRect(&bounds, point)) return true;
        return child && child->ContainsPoint(point);
    }
    void UpdateDate(bool date) {
        clockDate = date;
        for (size_t i = 0; i < rows.size(); ++i) {
            auto& row = rows[i];
            if (row.command == TrayDateCommand) {
                row.secondary = date ? L"on" : L"off";
                if (i < buttons.size()) SetWindowTextW(buttons[i], (row.label + L", " + row.secondary).c_str());
            }
        }
        RepaintButtons();
    }
    int Focused() const {
        for (size_t i = 0; i < buttons.size(); ++i) if (GetFocus() == buttons[i]) return static_cast<int>(i);
        return 0;
    }
    void Finish(UINT command = 0) {
        PreviewCursor(-1);
        if (parent) { parent->Finish(command); return; }
        if (done) return;
        result = command;
        done = true;
        CompleteEntrance();
        if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = nullptr; }
        if (child) child->HideBranch();
        if (window) {
            ShowWindow(window, SW_HIDE);
            PostMessageW(window, WM_NULL, 0, 0); // Wake GetMessage after a sent activation message.
        }
    }
    void RepaintRow(int index) const {
        if(index<0 || index>=static_cast<int>(buttons.size()) || !window)return;
        RECT r{};GetWindowRect(buttons[index],&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);InvalidateRect(window,&r,FALSE);
    }
    void RepaintCommand(UINT command) const {
        for(size_t i=0;i<rows.size();++i)if(rows[i].command==command){RepaintRow(static_cast<int>(i));break;}
    }
    void RepaintButtons() const {
        if (window) InvalidateRect(window, nullptr, FALSE);
    }
    void RefreshCursorIcon() {
        const int pixels=Px(MenuGlyphSize);
        if(cursorIcon && iconCursorMode==cursorMode && iconPixels==pixels && (cursorMode!=CursorDefault || iconLight==systemLight))return;
        const HICON replacement = reinterpret_cast<HICON>(SendMessageW(controller, TrayCursorIconMessage, Px(MenuGlyphSize), 0));
        if (replacement) {
            if (cursorIcon) DestroyIcon(cursorIcon);
            cursorIcon = replacement;iconCursorMode=cursorMode;iconPixels=pixels;iconLight=systemLight;
            RepaintButtons();
        }
    }
    void RefreshUpdate() {
        const auto state = static_cast<UpdateStatus>(SendMessageW(controller, TrayUpdateQueryMessage, 0, 0));
        if (UpdateBusy(state) && updateStatus != state) updateStarted = GetTickCount64();
        updateStatus = state;
        if (window) {
            if (kind == TrayGeneralCommand && IsWindowVisible(window) && UpdateBusy(state)) SetTimer(window, UpdateSpinnerTimer, 40, nullptr);
            else KillTimer(window, UpdateSpinnerTimer);
        }
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].command == TrayUpdateCommand) {
            rows[i].secondary = UpdateStatusLabel(state);
            if (i < buttons.size()) SetWindowTextW(buttons[i], (rows[i].label + L", " + rows[i].secondary).c_str());
        }
        RepaintButtons();
        if (child) child->RefreshUpdate();
    }
    void RefreshCursors() {
        if(parent)ClearPreviewCursors();
        auto* root = Root();
        root->cursorMode = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 1, 0));
        const auto mask = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 0, 0));
        root->hasCursorPack = HasCursorPack(mask);
        if (!parent) {
            for (size_t i = 0; i < rows.size(); ++i) {
                auto& row = rows[i];
                if (row.command != TrayCursorCommand && row.command != TrayCursorUnlockCommand) continue;
                row.command = hasCursorPack ? TrayCursorCommand : TrayCursorUnlockCommand;
                row.label = hasCursorPack ? L"cursor" : L"colors";
                row.secondary = hasCursorPack ? L"" : L"unlock";
                if (i < buttons.size()) {
                    SetWindowLongPtrW(buttons[i], GWLP_ID, row.command);
                    SetWindowTextW(buttons[i], (row.label + (row.secondary.empty() ? L"" : L", " + row.secondary)).c_str());
                }
            }
            if (window) {
                if (hasCursorPack) KillTimer(window, ColorPreviewTimer);
                else SetTimer(window, ColorPreviewTimer, 650, nullptr);
            }
            if (!hasCursorPack && child && child->kind == TrayCursorCommand) CloseChild(false);
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            auto& row = rows[i];
            if (row.command < CursorCommandBase || row.command > CursorCommandBase + CursorDisabled) continue;
            const auto cursorChoice = row.command - CursorCommandBase;
            row.enabled = (mask & (1u << cursorChoice)) != 0;
            row.checked = cursorChoice == root->cursorMode;
            if (i < buttons.size()) EnableWindow(buttons[i], row.enabled);
        }
        if (!parent) RefreshCursorIcon();
        RepaintButtons();
        if (child) child->RefreshCursors();
    }
    COLORREF SecondaryInk(bool highlighted) const {
        return highContrast ? (highlighted ? highlightedText : text) : light ? RGB(100, 100, 100) : RGB(170, 170, 170);
    }
    bool UpdateIndicators() {
        return checkIcon.Load(IDI_CHECK_LIGHT, Px(MenuGlyphSize), text, highlightedText)
            && chevronIcon.Load(IDI_CHEVRON_RIGHT_LIGHT, Px(MenuGlyphSize), SecondaryInk(false), SecondaryInk(true))
            && (kind != TrayGeneralCommand || externalIcon.Load(IDI_EXTERNAL_LIGHT, Px(MenuGlyphSize), SecondaryInk(false), SecondaryInk(true)));
    }
    bool UpdateTheme(bool systemMode) {
        if(parent && systemLight!=systemMode)ClearPreviewCursors();
        systemLight = systemMode;
        light = systemMode;
        HIGHCONTRASTW contrast{sizeof(contrast)};
        const bool high = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
            && (contrast.dwFlags & HCF_HIGHCONTRASTON);
        highContrast = high;
        const COLORREF bg = high ? GetSysColor(COLOR_MENU) : light ? RGB(249, 249, 249) : RGB(32, 32, 32);
        if (high) light = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) > 384;
        border = high ? GetSysColor(COLOR_MENUTEXT) : MenuBorder(light);
        const HBRUSH nextBackground = CreateSolidBrush(bg);
        const HBRUSH nextHighlight = CreateSolidBrush(high ? GetSysColor(COLOR_HIGHLIGHT) : light ? RGB(234, 234, 234) : RGB(54, 54, 54));
        const HBRUSH nextSeparator = CreateSolidBrush(border);
        if (!nextBackground || !nextHighlight || !nextSeparator) {
            if (nextBackground) DeleteObject(nextBackground);
            if (nextHighlight) DeleteObject(nextHighlight);
            if (nextSeparator) DeleteObject(nextSeparator);
            return false;
        }
        if (background) DeleteObject(background);
        if (highlight) DeleteObject(highlight);
        if (separator) DeleteObject(separator);
        background = nextBackground; highlight = nextHighlight; separator = nextSeparator;
        text = high ? GetSysColor(COLOR_MENUTEXT) : light ? RGB(26, 26, 26) : RGB(243, 243, 243);
        highlightedText = high ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text;
        if (!UpdateIndicators()) return false;
        if (!parent) {
            const auto automatic = static_cast<AutoThemeStatus>(SendMessageW(controller, TrayThemeQueryMessage, 1, 0));
            const bool autoSelected = automatic == AutoThemeStatus::Automatic || automatic == AutoThemeStatus::Locating;
            const bool darkInk = GetRValue(text) + GetGValue(text) + GetBValue(text) < 384;
            const int resources[]{darkInk ? IDI_ACRYLIC_OFF_LIGHT : IDI_ACRYLIC_OFF_DARK,
                darkInk ? IDI_ACRYLIC_SYSTEM_LIGHT : IDI_ACRYLIC_SYSTEM_DARK,
                darkInk ? IDI_ACRYLIC_PURE_LIGHT : IDI_ACRYLIC_PURE_DARK,
                autoSelected ? (darkInk ? IDI_SUNSET_LIGHT : IDI_SUNSET_DARK)
                    : systemLight ? (darkInk ? IDI_SUN_LIGHT : IDI_SUN_DARK) : (darkInk ? IDI_MOON_LIGHT : IDI_MOON_DARK),
                darkInk ? IDI_CALENDAR_OFF_LIGHT : IDI_CALENDAR_OFF_DARK,
                darkInk ? IDI_CALENDAR_LIGHT : IDI_CALENDAR_DARK,
                darkInk ? IDI_SETTINGS_LIGHT : IDI_SETTINGS_DARK};
            HICON* icons[]{&acrylicIcons[0], &acrylicIcons[1], &acrylicIcons[2], &themeIcon, &dateIcons[0], &dateIcons[1], &generalIcon};
            for (size_t i = 0; i < std::size(resources); ++i) {
                const auto next = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(resources[i]), IMAGE_ICON, Px(MenuGlyphSize), Px(MenuGlyphSize), 0));
                if (next) { if (*icons[i]) DestroyIcon(*icons[i]); *icons[i] = next; }
            }
            RefreshCursorIcon();
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i].command == TrayThemeCommand) {
                rows[i].label = L"system";
                rows[i].secondary = autoSelected ? L"auto"
                    : automatic == AutoThemeStatus::Denied ? L"location off" : automatic == AutoThemeStatus::Unavailable ? L"no location"
                    : automatic == AutoThemeStatus::Error ? L"try again" : systemLight ? L"light" : L"dark";
                if (i < buttons.size()) SetWindowTextW(buttons[i], (rows[i].label + L", " + rows[i].secondary).c_str());
            }
            if (!acrylicIcons[0] || !acrylicIcons[1] || !acrylicIcons[2] || !themeIcon || !dateIcons[0] || !dateIcons[1] || !generalIcon) return false;
        } else if (kind == TrayGeneralCommand) {
            if(!graphicsToken){Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&graphicsToken,&input,nullptr)!=Gdiplus::Ok)graphicsToken=0;}
            const bool darkInk = GetRValue(text) + GetGValue(text) + GetBValue(text) < 384;
            const int resources[]{darkInk ? IDI_UPDATE_LIGHT : IDI_UPDATE_DARK,
                darkInk ? IDI_PACKAGE_LIGHT : IDI_PACKAGE_DARK,
                darkInk ? IDI_PACKAGE_OPEN_LIGHT : IDI_PACKAGE_OPEN_DARK,
                darkInk ? IDI_GITHUB_LIGHT : IDI_GITHUB_DARK};
            HICON* icons[]{&updateIcon, &packageIcon, &packageOpenIcon, &githubIcon};
            for (size_t i = 0; i < std::size(resources); ++i) {
                const auto next = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(resources[i]), IMAGE_ICON, Px(MenuGlyphSize), Px(MenuGlyphSize), 0));
                if (next) { if (*icons[i]) DestroyIcon(*icons[i]); *icons[i] = next; }
            }
            if (graphicsToken && updateIcon) spinnerBitmap = SpinnerBitmap(updateIcon);
            if (!updateIcon || !packageIcon || !packageOpenIcon || !githubIcon || !externalIcon) return false;
        }
        if (window) {
            const BOOL dark = !light;
            DwmSetWindowAttribute(window, ImmersiveDarkMode, &dark, sizeof(dark));
            DwmSetWindowAttribute(window, BorderColor, &border, sizeof(border));
            ConfigureBackdrop();
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        if (child) child->UpdateTheme(systemMode);
        return true;
    }
    void DisableBackdrop() {
        acrylic = false;
        const int none = 1;
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
        const MARGINS margins{1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(window, &margins);
        SetAccent(0);
    }
    bool SetAccent(int state) {
        // Windows' documented transient backdrop is almost opaque on some
        // builds. Use the same native accent acrylic API as our legacy taskbar
        // backend, with an explicit translucent tint and no painted backdrop.
        struct Policy { int state; UINT flags; DWORD color; DWORD animation; };
        struct Data { int attribute; void* data; SIZE_T size; };
        using Set = BOOL(WINAPI*)(HWND, const Data*);
        const auto set = reinterpret_cast<Set>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        Policy policy{state, 0, MenuTint(light), 0};
        const Data data{19, &policy, sizeof(policy)};
        return set && set(window, &data);
    }
    void ConfigureBackdrop() {
        DWORD transparency = 1, bytes = sizeof(transparency);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &bytes);
        const int none = 1;
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
        const MARGINS full{-1, -1, -1, -1};
        acrylic = !highContrast && transparency && SUCCEEDED(DwmExtendFrameIntoClientArea(window, &full)) && SetAccent(4);
        if (!acrylic) DisableBackdrop();
    }
    void Focus(int index, int direction = 1) {
        if (buttons.empty()) return;
        PreviewCursor(-1);
        const auto count = static_cast<int>(buttons.size());
        for (int tried = 0; tried < count && !rows[static_cast<size_t>(index)].enabled; ++tried)
            index = (index + direction + count) % count;
        for (auto* level = this; level; level = level->parent) KillTimer(level->window, ChildCloseTimer);
        if (child && index != childIndex) child->HideBranch();
        keyboard = true;
        SetFocus(buttons[static_cast<size_t>(index)]);
        RepaintButtons();
    }
    void CloseChild(bool keyboardFocus) {
        KillTimer(window, ChildCloseTimer);
        if (!child || !child->window) return;
        child->HideBranch();
        SetForegroundWindow(window);
        if (keyboardFocus) Focus(std::max(0, childIndex));
        else SetFocus(buttons[static_cast<size_t>(hovered >= 0 ? hovered : std::max(0, childIndex))]);
    }
    void OpenChild(int index, bool fromKeyboard) {
        Root()->CompleteEntrance();
        if (index < 0 || index >= static_cast<int>(rows.size())) return;
        const UINT command = rows[static_cast<size_t>(index)].command;
        if (!HasSubmenu(command)) return;
        suppressedHover = -1;
        KillTimer(window, HoverTimer);
        KillTimer(window, ChildCloseTimer);
        if (child && childIndex != index) { child->HideBranch(); child.reset(); }
        if (!child) {
            child = std::make_unique<Popup>();
            childIndex = index;
            child->parent = this;
            child->controller = controller;
            child->kind = command;
            child->dpi = dpi;
            child->light = light;
            child->systemLight = systemLight;
            child->hoverDelay = hoverDelay;
            auto* root = Root();
            if (command == TrayCursorCommand) {
                const auto mask = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 0, 0));
                child->rows = CursorRows(mask, root->cursorMode);
            } else child->rows = GeneralRows();
            RECT bounds{}, row{};
            GetWindowRect(window, &bounds);
            GetWindowRect(buttons[static_cast<size_t>(index)], &row);
            if (!child->Create(window, {bounds.right - Px(2), row.top - Px(Padding)}, true)) {
                child.reset(); return;
            }
        }
        child->closing = false;
        ShowWindow(child->window, SW_SHOWNORMAL);
        SetForegroundWindow(child->window);
        child->RefreshUpdate();
        const UINT selected = command == TrayCursorCommand ? std::min(Root()->cursorMode, CursorDisabled) : 0;
        child->Focus(static_cast<int>(selected));
        child->keyboard = fromKeyboard;
        child->RepaintButtons();
    }
    void ClearPreviewCursors() {
        PreviewCursor(-1);
        for(auto& cursor:previewCursors){if(cursor)DestroyCursor(cursor);cursor=nullptr;}
        previewAttempted=0;previewCursor=nullptr;
    }
    void PreviewCursor(int index) {
        if (kind != TrayCursorCommand) return;
        // Hiding a window can queue WM_MOUSELEAVE while its old button bounds
        // still contain the pointer. Hidden/finished menus may only end previews.
        const bool live = !closing && !Root()->done && window && IsWindowVisible(window);
        const UINT next = live && index >= 0 && index < static_cast<int>(rows.size()) && rows[static_cast<size_t>(index)].enabled
            ? rows[static_cast<size_t>(index)].command - CursorCommandBase : CursorPreviewNone;
        if (next == previewMode) return;
        previewMode = next;
        if(next==CursorPreviewNone) {
            previewCursor=nullptr;SendMessageW(controller,TrayCursorPreviewMessage,next,0);
        } else {
            const UINT bit=1u<<next;
            if(!(previewAttempted & bit)) {
                previewAttempted|=bit;
                previewCursors[next]=reinterpret_cast<HCURSOR>(SendMessageW(controller,TrayCursorPreviewMessage,next,0));
            }
            previewCursor=previewCursors[next];
        }
        SetCursor(previewCursor?previewCursor:LoadCursorW(nullptr,IDC_ARROW));
    }
    void LeaveRow() {
        // WM_MOUSELEAVE can be queued after entering the next child button.
        // Resolve the pointer once to avoid briefly restoring between rows.
        POINT point{}; RECT bounds{};
        if (kind == TrayCursorCommand && GetCursorPos(&point)) {
            for (size_t i = 0; i < buttons.size(); ++i)
                if (GetWindowRect(buttons[i], &bounds) && PtInRect(&bounds, point)) { Hover(static_cast<int>(i)); return; }
        }
        Hover(-1);
    }
    void Hover(int index) {
        if (index >= 0 && !rows[static_cast<size_t>(index)].enabled) index = -1;
        PreviewCursor(index);
        if (index != suppressedHover) suppressedHover = -1;
        for (auto* level = parent; level; level = level->parent) {
            if (index >= 0) KillTimer(level->window, ChildCloseTimer);
            else if (level->child && IsWindowVisible(level->child->window))
                SetTimer(level->window, ChildCloseTimer, std::max(1u, level->hoverDelay), nullptr);
        }
        if (child && IsWindowVisible(child->window)) {
            if (index == childIndex) KillTimer(window, ChildCloseTimer);
            else SetTimer(window, ChildCloseTimer, std::max(1u, hoverDelay), nullptr);
        }
        if (hovered == index && !keyboard) return;
        const int previous=hovered;hovered = index;
        keyboard = false;
        RepaintRow(previous);RepaintRow(index);
        KillTimer(window, HoverTimer);
        if (index >= 0 && index != suppressedHover && HasSubmenu(rows[static_cast<size_t>(index)].command)
            && (!child || childIndex != index || !IsWindowVisible(child->window)))
            SetTimer(window, HoverTimer, std::max(1u, hoverDelay), nullptr);
    }
    bool PointerInChildMenu() const {
        POINT point{}; RECT bounds{};
        if (!GetCursorPos(&point)) return false;
        if (child && child->ContainsPoint(point)) return true;
        return childIndex >= 0 && GetWindowRect(buttons[static_cast<size_t>(childIndex)], &bounds) && PtInRect(&bounds, point);
    }
    void Activate(int index, bool fromKeyboard) {
        Root()->CompleteEntrance();
        if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[static_cast<size_t>(index)].enabled) return;
        const UINT command = rows[static_cast<size_t>(index)].command;
        if (HasSubmenu(command)) {
            if (!fromKeyboard && child && childIndex == index && IsWindowVisible(child->window)) {
                // A click collapses this branch. Movement within the same row
                // must not immediately reopen it, including a queued timer.
                KillTimer(window, HoverTimer);
                suppressedHover = index;
                CloseChild(false);
                hovered = index; keyboard = false;
                RepaintButtons();
            } else OpenChild(index, fromKeyboard);
        }
        else if (command == TrayExitCommand) Finish(command);
        else {
            // Settings share one live controller path. Keep the existing
            // windows and focus; reflect the actual state after applying.
            auto* root = Root();
            const UINT state = static_cast<UINT>(SendMessageW(root->controller, TraySettingMessage, command, 0));
            if (root->done) return;
            if (command == TrayGithubCommand && state) { Finish(); return; }
            if (command == TrayToggleCommand) {
                root->mode = DecodeAcrylicMode(static_cast<LONG>(state));
                for (size_t i = 0; i < root->rows.size(); ++i) if (root->rows[i].command == TrayToggleCommand) {
                    auto& row = root->rows[i]; row.secondary = AcrylicModeName(root->mode);
                    SetWindowTextW(root->buttons[i], (row.label + L", " + row.secondary).c_str());
                }
                root->RepaintButtons();
            } else if (command == TrayUpdateCommand) {
                root->RefreshUpdate();
            } else if (command == TrayThemeCommand) {
                root->UpdateTheme(state != 0);
            } else if (command == TrayDateCommand) {
                root->UpdateDate((state & 1) != 0);
            } else if (parent && command >= CursorCommandBase && command <= CursorCommandBase + CursorDisabled) {
                // Selection applies the full scheme once. End the local
                // preview without closing the menus.
                PreviewCursor(-1);
                root->cursorMode = state;
                root->RefreshCursorIcon();
                for (auto& row : rows) row.checked = row.command == CursorCommandBase + state;
                RepaintButtons();
            }
        }
    }
    bool Key(WPARAM key) {
        Root()->CompleteEntrance();
        const int current = Focused(), count = static_cast<int>(buttons.size());
        switch (key) {
        case VK_ESCAPE: case VK_LEFT:
            if (parent) parent->CloseChild(true);
            else if (key == VK_ESCAPE) Finish();
            return true;
        case VK_RIGHT:
            if (HasSubmenu(rows[static_cast<size_t>(current)].command)) OpenChild(current, true);
            return true;
        case VK_UP: Focus((current + count - 1) % count, -1); return true;
        case VK_DOWN: Focus((current + 1) % count); return true;
        case VK_TAB: { const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1; Focus((current + count + step) % count, step); return true; }
        case VK_HOME: Focus(0); return true;
        case VK_END: Focus(count - 1, -1); return true;
        case VK_RETURN: case VK_SPACE: Activate(current, true); return true;
        }
        return false;
    }
    void DrawLabel(HDC dc, RECT bounds, const std::wstring& label, COLORREF ink, UINT flags) {
        if(!menuText.Draw(dc,bounds,label,ink,flags)) {
            ++textFallbacks;
            MenuSurface::Text(dc,bounds,label,font,ink,flags);
        }
    }
    COLORREF LabelInk(const Row& row, bool highlighted) const {
        if (!row.enabled) return GetSysColor(COLOR_GRAYTEXT);
        // Preserve Windows contrast-theme colors, including the focus highlight.
        if (row.command == TrayExitCommand && !highContrast)
            return light ? RGB(164, 38, 44) : RGB(255, 153, 164);
        return highlighted ? highlightedText : text;
    }
    void DrawButtonContent(const DRAWITEMSTRUCT& item, ULONGLONG frameTime) {
        const auto found = std::find(buttons.begin(), buttons.end(), item.hwndItem);
        if (found == buttons.end()) return;
        const int index = static_cast<int>(found - buttons.begin());
        const auto& row = rows[static_cast<size_t>(index)];
        const bool focused = keyboard && (item.itemState & ODS_FOCUS);
        const bool highlighted = row.enabled && (hovered == index || focused || (item.itemState & ODS_SELECTED));
        if (highlighted) {
            const auto oldBrush = SelectObject(item.hDC, highlight);
            const auto oldPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
            RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, Px(6), Px(6));
            SelectObject(item.hDC, oldPen);
            SelectObject(item.hDC, oldBrush);
        }
        const auto oldFont = SelectObject(item.hDC, font);
        const COLORREF ink = LabelInk(row, highlighted);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, ink);
        RECT bounds = item.rcItem;
        bounds.left += Px(34);
        bounds.right -= Px(24);
        if (!row.secondary.empty()) {
            RECT status = item.rcItem; status.right -= Px(12);
            const int statusWidth=menuText.Width(row.secondary);
            SIZE fallback{};if(!statusWidth) GetTextExtentPoint32W(item.hDC,row.secondary.c_str(),static_cast<int>(row.secondary.size()),&fallback);
            status.left = status.right - (statusWidth?statusWidth:fallback.cx);
            DrawLabel(item.hDC, status, row.secondary, SecondaryInk(highlighted), DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            bounds.right = std::min(bounds.right, status.left - Px(12));
        }
        DrawLabel(item.hDC, bounds, row.label, ink, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        // Font line boxes include unused space. Align every glyph to the
        // label's visible ink instead, using the selected font at this DPI.
        const int cy = menuText.InkCenter(row.label,bounds);
        const HICON icon = row.command == TrayToggleCommand ? acrylicIcons[static_cast<size_t>(mode)]
            : row.command == TrayCursorCommand ? cursorIcon : row.command == TrayCursorUnlockCommand && !colorIcons.empty() ? colorIcons[colorFrame % colorIcons.size()] : row.command == TrayThemeCommand ? themeIcon
            : row.command == TrayDateCommand ? dateIcons[clockDate ? 1 : 0] : row.command == TrayGeneralCommand ? generalIcon : row.command == TrayUpdateCommand
                ? (updateStatus == UpdateStatus::Available ? packageOpenIcon : UpdateBusy(updateStatus) ? updateIcon : packageIcon)
                : row.command == TrayGithubCommand ? githubIcon : nullptr;
        // Cursor and Lucide glyphs share one square slot and row alignment.
        const int iconSize = Px(MenuGlyphSize);
        // The cursor's pointed silhouette looks high at the mathematical center.
        // Give its centered square a small optical adjustment without scaling it.
        const int iconCenter = row.command == TrayCursorCommand || row.command == TrayCursorUnlockCommand ? (item.rcItem.top + item.rcItem.bottom) / 2 + Px(1) : cy;
        const int iconTop = iconCenter - iconSize / 2;
        const int iconLeft = item.rcItem.left + Px(MenuIconInset) + (Px(MenuIconSize) - iconSize) / 2;
        if (row.command == TrayUpdateCommand && UpdateBusy(updateStatus) && spinnerBitmap && spinnerBitmap->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(item.hDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.TranslateTransform(static_cast<Gdiplus::REAL>(iconLeft) + iconSize / 2.0f, static_cast<Gdiplus::REAL>(iconTop) + iconSize / 2.0f);
            graphics.RotateTransform(static_cast<Gdiplus::REAL>((frameTime - updateStarted) % 1000) * .36f);
            graphics.DrawImage(spinnerBitmap.get(), -iconSize / 2.0f, -iconSize / 2.0f, static_cast<Gdiplus::REAL>(iconSize), static_cast<Gdiplus::REAL>(iconSize));
        } else if (icon) DrawIconEx(item.hDC, iconLeft, iconTop, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        if (row.checked) DrawIconEx(item.hDC, iconLeft, cy - iconSize / 2, checkIcon.Get(highlighted), iconSize, iconSize, 0, nullptr, DI_NORMAL);
        if (HasSubmenu(row.command)) DrawIconEx(item.hDC, item.rcItem.right - Px(12) - iconSize, cy - iconSize / 2,
            chevronIcon.Get(highlighted), iconSize, iconSize, 0, nullptr, DI_NORMAL);
        if (row.command == TrayGithubCommand && highlighted && externalIcon)
            DrawIconEx(item.hDC, item.rcItem.right - Px(12) - iconSize, cy - iconSize / 2,
                externalIcon.Get(highlighted), iconSize, iconSize, 0, nullptr, DI_NORMAL);
        if (focused) {
            RECT focus = item.rcItem;
            InflateRect(&focus, -Px(3), -Px(3));
            DrawFocusRect(item.hDC, &focus);
        }
        SelectObject(item.hDC, oldFont);
    }
    void Paint(HDC dc) {
        RECT bounds{},dirty{};
        GetClientRect(window, &bounds);
        if(GetClipBox(dc,&dirty)==ERROR || !IntersectRect(&dirty,&dirty,&bounds))return;
        const auto frameTime = GetTickCount64();
        if (acrylic) {
            if (MenuSurface::Paint(dc, dirty, [&](HDC layer) { PaintContent(layer, bounds, false, frameTime,dirty); })) return;
            DisableBackdrop();
        }
        PaintContent(dc, bounds, true, frameTime,dirty);
    }
    void PaintContent(HDC dc, const RECT& bounds, bool opaque, ULONGLONG frameTime,const RECT& dirty) {
        if (opaque) FillRect(dc, &dirty, background);
        int y = Px(Padding);
        for (const auto& row : rows) {
            if (row.divider) {
                RECT line{Px(Padding + 4), y + Px(SeparatorHeight) / 2,
                    bounds.right - Px(Padding + 4), y + Px(SeparatorHeight) / 2 + 1};
                FillRect(dc, &line, separator);
                y += Px(SeparatorHeight);
            }
            y += rowHeight;
        }
        // One ARGB surface owns all foreground pixels. Independently painted
        // child HWNDs can lose/overwrite alpha on glass. Keep real buttons for
        // accessibility, keyboard input and hit testing, but paint them here.
        for (size_t i=0; i<buttons.size(); ++i) {
            RECT position{}; GetWindowRect(buttons[i], &position);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&position),2);
            RECT intersection{};if(!IntersectRect(&intersection,&position,&dirty))continue;
            const int saved=SaveDC(dc);OffsetViewportOrgEx(dc,position.left,position.top,nullptr);
            DRAWITEMSTRUCT item{};item.hwndItem=buttons[i];item.CtlID=rows[i].command;item.hDC=dc;
            if(GetFocus()==buttons[i]) item.itemState|=ODS_FOCUS;
            GetClientRect(buttons[i],&item.rcItem);DrawButtonContent(item,frameTime);RestoreDC(dc,saved);
        }
    }
    bool Create(HWND owner, POINT anchor, bool submenu) {
        if (!UpdateTheme(systemLight)) return false;
        font = CreateSystemMenuFont(dpi);
        if (!background || !highlight || !separator || !font) return false;
        menuText.Initialize(font); // GDI remains a readable fallback if graphics initialization fails.
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
        // Reserve the same breathing room at the taskbar and screen edge.
        // Work-area coordinates also handle secondary/negative-coordinate monitors.
        RECT area = monitor.rcWork;
        InflateRect(&area, -Px(MenuEdgeGap), -Px(MenuEdgeGap));
        const int width = Px(parent ? 228 : 296);
        const int separators = static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const Row& row) { return row.divider; })) * Px(SeparatorHeight);
        rowHeight = std::min<int>(Px(40), (area.bottom - area.top - 2 * Px(Padding) - separators) / static_cast<int>(rows.size()));
        const int height = 2 * Px(Padding) + static_cast<int>(rows.size()) * rowHeight + separators;
        int x = anchor.x;
        if (submenu && x + width > area.right) {
            RECT ownerBounds{};
            GetWindowRect(owner, &ownerBounds);
            x = ownerBounds.left - width + Px(2);
        }
        x = std::clamp<LONG>(x, area.left, std::max(area.left, area.right - width));
        const int y = std::clamp(submenu ? anchor.y : anchor.y - height - Px(MenuEdgeGap), area.top,
            std::max(area.top, area.bottom - height));
        restingBounds = {x, y, x + width, y + height};
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, kind == TrayCursorCommand ? L"cursor" : kind == TrayGeneralCommand ? L"general" : L"jellyflower",
            WS_POPUP, x, y, width, height,
            owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!window) return false;
        const BOOL dark = !light;
        DwmSetWindowAttribute(window, CornerPreference, &RoundCorners, sizeof(RoundCorners));
        DwmSetWindowAttribute(window, ImmersiveDarkMode, &dark, sizeof(dark));
        DwmSetWindowAttribute(window, BorderColor, &border, sizeof(border));
        ConfigureBackdrop();
        int top = Px(Padding);
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].divider) top += Px(SeparatorHeight);
            const auto button = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", (rows[i].secondary.empty() ? rows[i].label : rows[i].label + L", " + rows[i].secondary).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, Px(Padding), top,
                width - 2 * Px(Padding), rowHeight, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(rows[i].command)),
                GetModuleHandleW(nullptr), nullptr);
            if (!button) return false;
            buttons.push_back(button);
            EnableWindow(button, rows[i].enabled);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            if (!SetWindowSubclass(button, ButtonProc, i, reinterpret_cast<DWORD_PTR>(this))) return false;
            top += rowHeight;
        }
        RefreshUpdate();
        if (!parent) {
            constexpr COLORREF fills[]{RGB(250,250,250), RGB(40,40,40), RGB(145,170,240), RGB(148,182,104), RGB(129,216,184), RGB(244,143,167), RGB(230,170,100), RGB(245,229,179), RGB(231,194,218)};
            if(!HasCursorPack(static_cast<UINT>(SendMessageW(controller,TrayCursorQueryMessage,0,0))))for (const auto fill : fills) colorIcons.push_back(CreateCursorColorIcon(Px(MenuGlyphSize), fill, fill == RGB(40,40,40) ? RGB(240,240,240) : RGB(45,45,45)));
            RefreshCursors();
        }
        return true;
    }
};

void ObserveMenuMouse(WPARAM message, POINT point) {
    if (message != WM_LBUTTONDOWN && message != WM_RBUTTONDOWN
        && message != WM_MBUTTONDOWN && message != WM_XBUTTONDOWN) return;
    const auto root = activePopup;
    if (!root || root->done || root->ContainsPoint(point) || PtInRect(&root->trayBounds, point)) return;
    // Non-activating taskbar/desktop controls need an explicit outside-press
    // path. Post dismissal and always let the original click reach its target.
    PostMessageW(root->window, WM_CANCELMODE, 0, 0);
}
LRESULT CALLBACK PopupMouseProc(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && data) ObserveMenuMouse(message, reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt);
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
    auto* popup = reinterpret_cast<Popup*>(data);
    if (message == WM_PAINT) { PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) popup->RepaintButtons();
    if (message == WM_MOUSEMOVE) {
        popup->Hover(static_cast<int>(id));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
    } else if (message == WM_MOUSELEAVE) {
        if (popup->hovered == static_cast<int>(id)) popup->LeaveRow();
    } else if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ButtonProc, id);
    return DefSubclassProc(window, message, wParam, lParam);
}
LRESULT CALLBACK PopupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* popup = reinterpret_cast<Popup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        popup = static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        popup->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
    }
    if (!popup) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SETCURSOR:
        if(LOWORD(lParam)==HTCLIENT && popup->previewCursor){SetCursor(popup->previewCursor);return TRUE;}
        break;
    case WM_DWMCOMPOSITIONCHANGED:
        popup->ConfigureBackdrop();
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: popup->Paint(reinterpret_cast<HDC>(wParam)); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        popup->Paint(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM: return TRUE; // Foreground is composed once by the parent.
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            for (size_t i = 0; i < popup->rows.size(); ++i)
                if (popup->rows[i].command == LOWORD(wParam)) { popup->Activate(static_cast<int>(i), false); break; }
        }
        return 0;
    case WM_MOUSEMOVE: {
        popup->Hover(-1);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE: popup->LeaveRow(); return 0;
    case WM_TIMER:
        if (wParam == EntranceTimer) {
            popup->AdvanceEntrance(GetTickCount64());
        } else if (wParam == ColorPreviewTimer) {
            ++popup->colorFrame;
            popup->RepaintCommand(TrayCursorUnlockCommand);
        } else if (wParam == UpdateSpinnerTimer) {
            popup->RepaintCommand(TrayUpdateCommand);
        } else if (wParam == HoverTimer) { KillTimer(window, HoverTimer); if (popup->hovered >= 0 && popup->hovered != popup->suppressedHover) popup->OpenChild(popup->hovered, false); }
        else if (wParam == ChildCloseTimer) {
            KillTimer(window, ChildCloseTimer);
            if (!popup->PointerInChildMenu()) popup->CloseChild(false);
        } else if (wParam == TrayDeactivateTimer) {
            // A press on our own tray icon loses activation before the shell's
            // selection callback. Let that callback toggle the existing menu.
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
                KillTimer(window, TrayDeactivateTimer);
                popup->Finish();
            }
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) KillTimer(popup->Root()->window, TrayDeactivateTimer);
        if (LOWORD(wParam) == WA_INACTIVE && !popup->closing && !popup->Root()->done) {
            const HWND target = reinterpret_cast<HWND>(lParam);
            const Popup* root = popup->Root();
            const bool related = const_cast<Popup*>(root)->Target(target) != nullptr;
            if (!related) {
                POINT point{};
                if (GetCursorPos(&point) && PtInRect(&root->trayBounds, point))
                    SetTimer(root->window, TrayDeactivateTimer, std::max(200u, GetDoubleClickTime()), nullptr);
                else popup->Finish();
            }
        }
        return 0;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: {
        auto* root = popup->Root();
        if (!MenuAnimationsEnabled()) root->CompleteEntrance();
        if (!root->done) root->UpdateTheme(SendMessageW(root->controller, TrayThemeQueryMessage, 0, 0) != 0);
        return 0;
    }
    case WM_CANCELMODE: case WM_CLOSE: case WM_DPICHANGED: case WM_DISPLAYCHANGE: popup->Finish(); return 0;
    case WM_DESTROY:
        popup->window = nullptr;
        if (!popup->parent) popup->done = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

void RefreshUpdateMenu(HWND owner) {
    if (activePopup && activePopup->controller == owner && !activePopup->done) activePopup->RefreshUpdate();
}
void RefreshCursorMenu(HWND owner) {
    if (activePopup && activePopup->controller == owner && !activePopup->done) activePopup->RefreshCursors();
}
void RefreshThemeMenu(HWND owner) {
    if (activePopup && activePopup->controller == owner && !activePopup->done)
        activePopup->UpdateTheme(SendMessageW(owner, TrayThemeQueryMessage, 0, 0) != 0);
}
UINT ShowTrayMenu(HWND owner, POINT anchor, UINT dpi, AcrylicMode mode, bool light, UINT cursorMode, const RECT& trayBounds) {
    if (activePopup) { activePopup->Finish(); return 0; }
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = PopupProc;
    wc.lpszClassName = PopupClass;
    wc.style = CS_DROPSHADOW;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
    Popup popup;
    popup.dpi = dpi ? dpi : 96;
    popup.light = light;
    popup.systemLight = SendMessageW(owner, TrayThemeQueryMessage, 0, 0) != 0;
    popup.mode = mode;
    popup.controller = owner;
    popup.cursorMode = cursorMode;
    popup.trayBounds = trayBounds;
    const UINT clockState = static_cast<UINT>(SendMessageW(owner, TrayDateQueryMessage, 0, 0));
    popup.clockDate = (clockState & 1) != 0;
    SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &popup.hoverDelay, 0);
    popup.rows = RootRows(mode, popup.clockDate, popup.systemLight, static_cast<UINT>(SendMessageW(owner, TrayCursorQueryMessage, 0, 0)));
    popup.RefreshUpdate();
    if (!popup.Create(owner, anchor, false)) return 0;
    activePopup = &popup;
    struct Reset { ~Reset() { activePopup = nullptr; } } reset;
    // Startup uses a hidden controller. Consume any initial show override on
    // the still-hidden popup before displaying it, then activate only the menu.
    ShowWindow(popup.window, SW_HIDE);
    popup.BeginEntrance();
    ShowWindow(popup.window, SW_SHOWNORMAL);
    SetForegroundWindow(popup.window);
    SetFocus(popup.buttons[0]);
    RedrawWindow(popup.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    // Active only while this menu is open; runs on our existing message loop.
    popup.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, PopupMouseProc, GetModuleHandleW(nullptr), 0);
    MSG message{};
    while (!popup.done) {
        const BOOL next = GetMessageW(&message, nullptr, 0, 0);
        if (next <= 0) {
            if (next == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        Popup* target = popup.Target(message.hwnd);
        if (target && message.message == WM_KEYDOWN && target->Key(message.wParam)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return popup.result;
}
} // namespace acrylic

// SPDX-License-Identifier: GPL-3.0-or-later
#include "popup.hpp"
#include "tray.hpp"
#include "cursors.hpp"
#include "resource.h"
#include "updates.hpp"
#include "version.hpp"
#include "menu_surface.hpp"
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
    rows.push_back({TrayCursorUnlockCommand, L"unlock colors", true});
    return rows;
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
bool HasSubmenu(UINT command) { return command == TrayCursorCommand; }
struct Popup;
Popup* activePopup = nullptr;
LRESULT CALLBACK PopupProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK PopupMouseProc(int, WPARAM, LPARAM);

int LabelCenter(HDC dc, const std::wstring& label, const RECT& bounds) {
    const int fallback = (bounds.top + bounds.bottom) / 2;
    TEXTMETRICW metrics{};
    RECT layout{};
    const int height = DrawTextW(dc, label.c_str(), -1, &layout, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    if (!height || !GetTextMetricsW(dc, &metrics)) return fallback;
    const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    int top = 0, bottom = 0;
    bool found = false;
    for (const auto character : label) {
        GLYPHMETRICS glyph{};
        if (GetGlyphOutlineW(dc, character, GGO_METRICS, &glyph, 0, nullptr, &identity) == GDI_ERROR || !glyph.gmBlackBoxY) continue;
        const int upper = -glyph.gmptGlyphOrigin.y;
        const int lower = upper + static_cast<int>(glyph.gmBlackBoxY);
        top = found ? std::min(top, upper) : upper;
        bottom = found ? std::max(bottom, lower) : lower;
        found = true;
    }
    if (!found) return fallback;
    const int baseline = bounds.top + (bounds.bottom - bounds.top - height) / 2 + metrics.tmAscent;
    return MulDiv(2 * baseline + top + bottom, 1, 2);
}

struct Popup {
    HWND window = nullptr, controller = nullptr;
    std::vector<HWND> buttons;
    std::vector<Row> rows;
    std::unique_ptr<Popup> child;
    Popup* parent = nullptr;
    HFONT font = nullptr;
    HHOOK mouseHook = nullptr;
    HICON acrylicIcons[3]{}, dateIcons[2]{}, cursorIcon = nullptr, themeIcon = nullptr, updateIcon = nullptr;
    HICON packageIcon = nullptr, packageOpenIcon = nullptr;
    ULONG_PTR graphicsToken = 0;
    std::unique_ptr<Gdiplus::Bitmap> spinnerBitmap;
    UpdateStatus updateStatus = UpdateStatus::Idle;
    ULONGLONG updateStarted = 0;
    HBRUSH background = nullptr, highlight = nullptr, separator = nullptr;
    COLORREF text = 0, highlightedText = 0, border = 0;
    UINT dpi = 96, result = 0, cursorMode = 0, hoverDelay = 400;
    RECT trayBounds{};
    int hovered = -1, childIndex = -1, rowHeight = 40;
    UINT kind = 0;
    bool clockDate = true;
    AcrylicMode mode = AcrylicMode::System;
    bool light = false, systemLight = false, keyboard = false, done = false, closing = false;
    bool acrylic = false, highContrast = false;
    Popup() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) != Gdiplus::Ok) graphicsToken = 0;
    }
    ~Popup() {
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
        if (background) DeleteObject(background);
        if (highlight) DeleteObject(highlight);
        if (separator) DeleteObject(separator);
        spinnerBitmap.reset();
        if (graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken);
    }
    int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi), 96); }
    Popup* Root() { return parent ? parent->Root() : this; }
    Popup* Target(HWND target) {
        if (target == window || IsChild(window, target)) return this;
        return child ? child->Target(target) : nullptr;
    }
    void HideBranch() {
        closing = true;
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
        if (parent) { parent->Finish(command); return; }
        if (done) return;
        result = command;
        done = true;
        if (mouseHook) { UnhookWindowsHookEx(mouseHook); mouseHook = nullptr; }
        if (child) child->HideBranch();
        if (window) {
            ShowWindow(window, SW_HIDE);
            PostMessageW(window, WM_NULL, 0, 0); // Wake GetMessage after a sent activation message.
        }
    }
    void RepaintButtons() const {
        for (const auto button : buttons) if (button) InvalidateRect(button, nullptr, FALSE);
    }
    void RefreshCursorIcon() {
        const HICON replacement = reinterpret_cast<HICON>(SendMessageW(controller, TrayCursorIconMessage, Px(MenuGlyphSize), 0));
        if (replacement) {
            if (cursorIcon) DestroyIcon(cursorIcon);
            cursorIcon = replacement;
            RepaintButtons();
        }
    }
    void RefreshUpdate() {
        const auto state = static_cast<UpdateStatus>(SendMessageW(controller, TrayUpdateQueryMessage, 0, 0));
        if (state == UpdateStatus::Checking && updateStatus != state) updateStarted = GetTickCount64();
        updateStatus = state;
        if (window) {
            if (state == UpdateStatus::Checking) SetTimer(window, UpdateSpinnerTimer, 40, nullptr);
            else KillTimer(window, UpdateSpinnerTimer);
        }
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].command == TrayUpdateCommand) {
            rows[i].secondary = UpdateStatusLabel(state);
            if (i < buttons.size()) SetWindowTextW(buttons[i], (rows[i].label + L", " + rows[i].secondary).c_str());
        }
        RepaintButtons();
    }
    void RefreshCursors() {
        auto* root = Root();
        root->cursorMode = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 1, 0));
        const auto mask = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 0, 0));
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
    bool UpdateTheme(bool systemMode) {
        systemLight = systemMode;
        light = systemMode;
        HIGHCONTRASTW contrast{sizeof(contrast)};
        const bool high = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
            && (contrast.dwFlags & HCF_HIGHCONTRASTON);
        highContrast = high;
        const COLORREF bg = high ? GetSysColor(COLOR_MENU) : light ? RGB(249, 249, 249) : RGB(32, 32, 32);
        if (high) light = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) > 384;
        border = high ? GetSysColor(COLOR_MENUTEXT) : light ? RGB(210, 210, 210) : RGB(69, 69, 69);
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
        if (!parent) {
            const bool darkInk = GetRValue(text) + GetGValue(text) + GetBValue(text) < 384;
            const int resources[]{darkInk ? IDI_ACRYLIC_OFF_LIGHT : IDI_ACRYLIC_OFF_DARK,
                darkInk ? IDI_ACRYLIC_SYSTEM_LIGHT : IDI_ACRYLIC_SYSTEM_DARK,
                darkInk ? IDI_ACRYLIC_PURE_LIGHT : IDI_ACRYLIC_PURE_DARK,
                systemLight ? (darkInk ? IDI_MOON_LIGHT : IDI_MOON_DARK) : (darkInk ? IDI_SUN_LIGHT : IDI_SUN_DARK),
                darkInk ? IDI_CALENDAR_OFF_LIGHT : IDI_CALENDAR_OFF_DARK,
                darkInk ? IDI_CALENDAR_LIGHT : IDI_CALENDAR_DARK,
                darkInk ? IDI_UPDATE_LIGHT : IDI_UPDATE_DARK,
                darkInk ? IDI_PACKAGE_LIGHT : IDI_PACKAGE_DARK,
                darkInk ? IDI_PACKAGE_OPEN_LIGHT : IDI_PACKAGE_OPEN_DARK};
            HICON* icons[]{&acrylicIcons[0], &acrylicIcons[1], &acrylicIcons[2], &themeIcon, &dateIcons[0], &dateIcons[1], &updateIcon, &packageIcon, &packageOpenIcon};
            for (size_t i = 0; i < std::size(resources); ++i) {
                const auto next = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(resources[i]), IMAGE_ICON, Px(MenuGlyphSize), Px(MenuGlyphSize), 0));
                if (next) { if (*icons[i]) DestroyIcon(*icons[i]); *icons[i] = next; }
            }
            if (graphicsToken && updateIcon) spinnerBitmap = SpinnerBitmap(updateIcon);
            RefreshCursorIcon();
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i].command == TrayThemeCommand) {
                rows[i].label = systemLight ? L"dark mode" : L"light mode";
                if (i < buttons.size()) SetWindowTextW(buttons[i], rows[i].label.c_str());
            }
            if (!acrylicIcons[0] || !acrylicIcons[1] || !acrylicIcons[2] || !themeIcon || !dateIcons[0] || !dateIcons[1]) return false;
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
    }
    void ConfigureBackdrop() {
        // DWMSBT_TRANSIENTWINDOW is the documented Windows 11 desktop acrylic
        // backdrop. Unsupported systems and high contrast keep a solid menu.
        const int transient = 3;
        const MARGINS full{-1, -1, -1, -1};
        acrylic = !highContrast && SUCCEEDED(DwmExtendFrameIntoClientArea(window, &full))
            && SUCCEEDED(DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(38), &transient, sizeof(transient)));
        if (!acrylic) DisableBackdrop();
    }
    void Focus(int index, int direction = 1) {
        if (buttons.empty()) return;
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
        if (index < 0 || index >= static_cast<int>(rows.size())) return;
        const UINT command = rows[static_cast<size_t>(index)].command;
        if (!HasSubmenu(command)) return;
        KillTimer(window, HoverTimer);
        KillTimer(window, ChildCloseTimer);
        if (child && childIndex != index) { child->HideBranch(); child.reset(); }
        if (!child) {
            child = std::make_unique<Popup>();
            childIndex = index;
            child->parent = this;
            child->kind = command;
            child->dpi = dpi;
            child->light = light;
            child->systemLight = systemLight;
            child->hoverDelay = hoverDelay;
            auto* root = Root();
            const auto mask = static_cast<UINT>(SendMessageW(root->controller, TrayCursorQueryMessage, 0, 0));
            child->rows = CursorRows(mask, root->cursorMode);
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
        const UINT selected = std::min(Root()->cursorMode, CursorDisabled);
        child->Focus(static_cast<int>(selected));
        child->keyboard = fromKeyboard;
        child->RepaintButtons();
    }
    void Hover(int index) {
        if (index >= 0 && !rows[static_cast<size_t>(index)].enabled) index = -1;
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
        hovered = index;
        keyboard = false;
        RepaintButtons();
        KillTimer(window, HoverTimer);
        if (index >= 0 && HasSubmenu(rows[static_cast<size_t>(index)].command)
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
        if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[static_cast<size_t>(index)].enabled) return;
        const UINT command = rows[static_cast<size_t>(index)].command;
        if (HasSubmenu(command)) OpenChild(index, fromKeyboard);
        else if (command == TrayExitCommand) Finish(command);
        else {
            // Settings share one live controller path. Keep the existing
            // windows and focus; reflect the actual state after applying.
            auto* root = Root();
            const UINT state = static_cast<UINT>(SendMessageW(root->controller, TraySettingMessage, command, 0));
            if (root->done) return;
            if (command == TrayToggleCommand) {
                root->mode = DecodeAcrylicMode(static_cast<LONG>(state));
                auto& row = root->rows[0];
                row.secondary = AcrylicModeName(root->mode);
                SetWindowTextW(root->buttons[0], (row.label + L", " + row.secondary).c_str());
                root->RepaintButtons();
            } else if (command == TrayUpdateCommand) {
                root->RefreshUpdate();
            } else if (command == TrayThemeCommand) {
                root->UpdateTheme(state != 0);
            } else if (command == TrayDateCommand) {
                root->UpdateDate((state & 1) != 0);
            } else if (parent && command >= CursorCommandBase && command <= CursorCommandBase + CursorDisabled) {
                root->cursorMode = state;
                root->RefreshCursorIcon();
                for (auto& row : rows) row.checked = row.command == CursorCommandBase + state;
                RepaintButtons();
            }
        }
    }
    bool Key(WPARAM key) {
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
    void DrawButton(const DRAWITEMSTRUCT& item) {
        const auto frameTime = GetTickCount64();
        if (acrylic) {
            if (MenuSurface::Paint(item.hDC, item.rcItem, [&](HDC dc) {
                auto layer = item; layer.hDC = dc; DrawButtonContent(layer, false, frameTime);
            })) return;
            DisableBackdrop();
        }
        DrawButtonContent(item, true, frameTime);
    }
    void DrawButtonContent(const DRAWITEMSTRUCT& item, bool opaque, ULONGLONG frameTime) {
        const auto found = std::find(buttons.begin(), buttons.end(), item.hwndItem);
        if (found == buttons.end()) return;
        const int index = static_cast<int>(found - buttons.begin());
        const auto& row = rows[static_cast<size_t>(index)];
        if (opaque) FillRect(item.hDC, &item.rcItem, background);
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
        const COLORREF ink = !row.enabled ? GetSysColor(COLOR_GRAYTEXT) : highlighted ? highlightedText : text;
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, ink);
        RECT bounds = item.rcItem;
        bounds.left += Px(34);
        bounds.right -= Px(24);
        if (!row.secondary.empty()) {
            RECT status = item.rcItem; status.right -= Px(12);
            SIZE statusSize{};
            GetTextExtentPoint32W(item.hDC, row.secondary.c_str(), static_cast<int>(row.secondary.size()), &statusSize);
            status.left = status.right - statusSize.cx;
            SetTextColor(item.hDC, light ? RGB(100, 100, 100) : RGB(170, 170, 170));
            HIGHCONTRASTW contrast{sizeof(contrast)};
            if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) SetTextColor(item.hDC, ink);
            DrawTextW(item.hDC, row.secondary.c_str(), -1, &status, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SetTextColor(item.hDC, ink);
            bounds.right = std::min(bounds.right, status.left - Px(12));
        }
        DrawTextW(item.hDC, row.label.c_str(), -1, &bounds, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        const auto pen = CreatePen(PS_SOLID, std::max(1, Px(1)), ink);
        const auto oldPen = SelectObject(item.hDC, pen);
        // Font line boxes include unused space. Align every glyph to the
        // label's visible ink instead, using the selected font at this DPI.
        const int cy = LabelCenter(item.hDC, row.label, bounds);
        const HICON icon = row.command == TrayToggleCommand ? acrylicIcons[static_cast<size_t>(mode)]
            : row.command == TrayCursorCommand ? cursorIcon : row.command == TrayThemeCommand ? themeIcon
            : row.command == TrayDateCommand ? dateIcons[clockDate ? 1 : 0] : row.command == TrayUpdateCommand
                ? (updateStatus == UpdateStatus::Available ? packageOpenIcon : updateStatus == UpdateStatus::Checking ? updateIcon : packageIcon) : nullptr;
        // Cursor and Lucide glyphs share one square slot and row alignment.
        const int iconSize = Px(MenuGlyphSize);
        // The cursor's pointed silhouette looks high at the mathematical center.
        // Give its centered square a small optical adjustment without scaling it.
        const int iconCenter = row.command == TrayCursorCommand ? (item.rcItem.top + item.rcItem.bottom) / 2 + Px(1) : cy;
        const int iconTop = iconCenter - iconSize / 2;
        const int iconLeft = item.rcItem.left + Px(MenuIconInset) + (Px(MenuIconSize) - iconSize) / 2;
        if (row.command == TrayUpdateCommand && updateStatus == UpdateStatus::Checking && spinnerBitmap && spinnerBitmap->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(item.hDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.TranslateTransform(static_cast<Gdiplus::REAL>(iconLeft) + iconSize / 2.0f, static_cast<Gdiplus::REAL>(iconTop) + iconSize / 2.0f);
            graphics.RotateTransform(static_cast<Gdiplus::REAL>((frameTime - updateStarted) % 1000) * .36f);
            graphics.DrawImage(spinnerBitmap.get(), -iconSize / 2.0f, -iconSize / 2.0f, static_cast<Gdiplus::REAL>(iconSize), static_cast<Gdiplus::REAL>(iconSize));
        } else if (icon) DrawIconEx(item.hDC, iconLeft, iconTop, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        if (row.checked) {
            POINT mark[]{{Px(12), cy}, {Px(16), cy + Px(4)}, {Px(23), cy - Px(4)}};
            Polyline(item.hDC, mark, 3);
        }
        if (HasSubmenu(row.command)) {
            const int right = item.rcItem.right - Px(14);
            POINT chevron[]{{right - Px(3), cy - Px(4)}, {right + Px(1), cy}, {right - Px(3), cy + Px(4)}};
            Polyline(item.hDC, chevron, 3);
        }
        SelectObject(item.hDC, oldPen);
        DeleteObject(pen);
        if (focused) {
            RECT focus = item.rcItem;
            InflateRect(&focus, -Px(3), -Px(3));
            DrawFocusRect(item.hDC, &focus);
        }
        SelectObject(item.hDC, oldFont);
    }
    void Paint(HDC dc) {
        RECT bounds{};
        GetClientRect(window, &bounds);
        if (acrylic) {
            if (MenuSurface::Paint(dc, bounds, [&](HDC layer) { PaintContent(layer, bounds, false); })) return;
            DisableBackdrop();
        }
        PaintContent(dc, bounds, true);
    }
    void PaintContent(HDC dc, const RECT& bounds, bool opaque) const {
        if (opaque) FillRect(dc, &bounds, background);
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
    }
    bool Create(HWND owner, POINT anchor, bool submenu) {
        if (!UpdateTheme(systemLight)) return false;
        font = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!background || !highlight || !separator || !font) return false;
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
        const int width = Px(parent ? 228 : 296);
        const int separators = static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const Row& row) { return row.divider; })) * Px(SeparatorHeight);
        rowHeight = std::min<int>(Px(40), (monitor.rcWork.bottom - monitor.rcWork.top - 2 * Px(Padding) - separators) / static_cast<int>(rows.size()));
        const int height = 2 * Px(Padding) + static_cast<int>(rows.size()) * rowHeight + separators;
        int x = anchor.x;
        if (submenu && x + width > monitor.rcWork.right) {
            RECT ownerBounds{};
            GetWindowRect(owner, &ownerBounds);
            x = ownerBounds.left - width + Px(2);
        }
        x = std::clamp<LONG>(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
        const int y = std::clamp(submenu ? anchor.y : anchor.y - height - Px(6), monitor.rcWork.top,
            std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, kind == TrayCursorCommand ? L"cursor" : L"jellyflower",
            WS_POPUP | WS_CLIPCHILDREN, x, y, width, height,
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
            const auto button = CreateWindowExW(0, L"BUTTON", (rows[i].secondary.empty() ? rows[i].label : rows[i].label + L", " + rows[i].secondary).c_str(),
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
        if (!parent && updateStatus == UpdateStatus::Checking) SetTimer(window, UpdateSpinnerTimer, 40, nullptr);
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
    if (message == WM_MOUSEMOVE) {
        popup->Hover(static_cast<int>(id));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
    } else if (message == WM_MOUSELEAVE) {
        if (popup->hovered == static_cast<int>(id)) popup->Hover(-1);
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
    case WM_DRAWITEM: popup->DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
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
    case WM_MOUSELEAVE: popup->Hover(-1); return 0;
    case WM_TIMER:
        if (wParam == UpdateSpinnerTimer) {
            for (size_t i = 0; i < popup->rows.size(); ++i) if (popup->rows[i].command == TrayUpdateCommand)
                InvalidateRect(popup->buttons[i], nullptr, FALSE);
        } else if (wParam == HoverTimer) { KillTimer(window, HoverTimer); if (popup->hovered >= 0) popup->OpenChild(popup->hovered, false); }
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
    popup.rows = {{TrayToggleCommand, L"acrylic", false, false, AcrylicModeName(mode)},
        {TrayDateCommand, L"date", false, false, popup.clockDate ? L"on" : L"off"}, {TrayCursorCommand, L"cursor"}, {TrayThemeCommand, popup.systemLight ? L"dark mode" : L"light mode"},
        {TrayUpdateCommand, std::wstring(L"v") + AppVersion, true}, {TrayExitCommand, L"exit"}};
    popup.RefreshUpdate();
    if (!popup.Create(owner, anchor, false)) return 0;
    activePopup = &popup;
    struct Reset { ~Reset() { activePopup = nullptr; } } reset;
    // Startup uses a hidden controller. Consume any initial show override on
    // the still-hidden popup before displaying it, then activate only the menu.
    ShowWindow(popup.window, SW_HIDE);
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

// SPDX-License-Identifier: GPL-3.0-or-later
// The method and the re-measure follow m417z's GPLv3 "Taskbar height and icon
// size" Windhawk mod, version 1.3.11. See THIRD_PARTY_NOTICES.md.
#include "bar_height.hpp"
#include <shellscalingapi.h>
#include <atomic>

namespace acrylic {
namespace {
using GetMinSize = void (*)(void* self, HMONITOR monitor, SIZE* size);
std::atomic<LONG> height{0};
std::atomic<GetMinSize> stock{nullptr};
void** patched = nullptr;

void TallerMinSize(void* self, HMONITOR monitor, SIZE* size) {
    stock.load()(self, monitor, size);
    const LONG dips = height.load();
    UINT dpiX = 0, dpiY = 0;
    if (dips > 0 && size && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiY)
        size->cy = MulDiv(dips, static_cast<int>(dpiY), 96);
}
void* Ours() noexcept { return reinterpret_cast<void*>(&TallerMinSize); }
bool Write(void** slot, void* value) noexcept {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof(void*), protection, &protection);
    return true;
}
DWORD ImageSize(const BYTE* module) noexcept {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage : 0;
}
}
HRESULT SetBarHeight(BYTE* module, DWORD slotRva, DWORD functionRva, LONG dips) noexcept {
    if (dips <= 0) {
        height = 0;
        if (patched && *patched == Ours()) Write(patched, reinterpret_cast<void*>(stock.load()));
        patched = nullptr;
        return S_OK;
    }
    if (!module || !slotRva || !functionRva) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const DWORD size = ImageSize(module);
    if (size < sizeof(void*) || slotRva % sizeof(void*) || slotRva > size - sizeof(void*) || functionRva >= size)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    auto** slot = reinterpret_cast<void**>(module + slotRva);
    void* const expected = module + functionRva;
    if (*slot != expected && *slot != Ours()) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    stock = reinterpret_cast<GetMinSize>(expected);
    height = dips;
    if (*slot != Ours() && !Write(slot, Ours())) { height = 0; return HRESULT_FROM_WIN32(GetLastError()); }
    patched = slot;
    return S_OK;
}
LONG BarHeight() noexcept { return height.load(); }
void RemeasureTaskbar() noexcept {
    // TrayUI::_HandleSettingChange measures the bar and resets the work area.
    if (const HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr)) SendMessageW(bar, WM_SETTINGCHANGE, SPI_SETLOGICALDPIOVERRIDE, 0);
}
}

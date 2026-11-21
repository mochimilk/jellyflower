// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace acrylic {
// Where Taskbar.dll keeps TrayUI::GetMinSize(HMONITOR, SIZE*), as RVAs: the
// vtable slot, and the stock method it must hold (see bar_height.hpp).
struct MinSizeSlot { DWORD slot = 0, function = 0; };
// A module's public-symbol file name and symbol-store id (GUID and age), from
// its CodeView record. Empty when it has none.
struct SymbolKey { std::wstring pdb, id; };
SymbolKey ReadSymbolKey(const wchar_t* module);
// The RVA of the slot holding `function` in the vtable at `table`, in a module
// mapped as an image (LOAD_LIBRARY_AS_IMAGE_RESOURCE); 0 when not found.
DWORD FindVtableSlot(const BYTE* image, DWORD table, DWORD function) noexcept;
// Blocking. Returns the slot for the installed Taskbar.dll from the cache, or
// downloads Taskbar.dll's public symbols from Microsoft once (about 7 MB),
// reads the slot, caches it under HKCU\Software\jellyflower and deletes them.
HRESULT LocateMinSize(MinSizeSlot& slot) noexcept;
// Whether the installed Taskbar.dll's slot is already cached, so a lookup
// needs no download. Reads only the DLL's header and the registry.
bool MinSizeCached() noexcept;

// Runs LocateMinSize once on a worker thread that holds only this result.
class MinSizeLookup {
public:
    void Start();
    bool Started() const noexcept { return shared_ != nullptr; }
    // True once finished, with the slot and the result.
    bool Ready(MinSizeSlot& slot, HRESULT& result) const;
private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};
}

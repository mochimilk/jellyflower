// SPDX-License-Identifier: GPL-3.0-or-later
// Offline by default. `BarHeight --live` also looks up the installed
// Taskbar.dll's slot from Microsoft's symbol server (network, writes the cache).
#include "../src/bar_height.hpp"
#include "../src/taskbar_slot.hpp"
#include <shellscalingapi.h>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include <vector>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
using MinSize = void (*)(void*, HMONITOR, SIZE*);
int calls = 0;
// Stand-ins for TrayUI::GetMinSize and a neighbour, in a vtable in this module.
void StockMinSize(void*, HMONITOR, SIZE* size) { ++calls; size->cx = 1000; size->cy = 144; }
void Other(void*, HMONITOR, SIZE*) {}
void* table[3] = {reinterpret_cast<void*>(&Other), reinterpret_cast<void*>(&StockMinSize), reinterpret_cast<void*>(&Other)};

void Slot() {
    auto* module = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    const auto slot = static_cast<DWORD>(reinterpret_cast<BYTE*>(&table[1]) - module);
    const auto function = static_cast<DWORD>(reinterpret_cast<BYTE*>(&StockMinSize) - module);
    const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    Check(SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)), "monitor DPI");
    auto call = [&] { SIZE size{}; calls = 0; reinterpret_cast<MinSize>(table[1])(nullptr, monitor, &size); return size; };

    Check(acrylic::BarHeight() == 0 && call().cy == 144, "stock before anything");
    Check(SUCCEEDED(acrylic::SetBarHeight(module, slot, function, 0)) && table[1] == reinterpret_cast<void*>(&StockMinSize), "stock height writes nothing");
    Check(SUCCEEDED(acrylic::SetBarHeight(module, slot, function, 64)) && acrylic::BarHeight() == 64, "taller bar answered");
    Check(table[1] != reinterpret_cast<void*>(&StockMinSize) && table[0] == reinterpret_cast<void*>(&Other) && table[2] == reinterpret_cast<void*>(&Other), "only the one slot changes");
    auto size = call();
    Check(calls == 1 && size.cx == 1000 && size.cy == MulDiv(64, static_cast<int>(dpiY), 96), "stock method runs first, then the height at the monitor's DPI");
    Check(SUCCEEDED(acrylic::SetBarHeight(module, slot, function, 72)) && call().cy == MulDiv(72, static_cast<int>(dpiY), 96) && calls == 1, "height changes without wrapping twice");
    Check(SUCCEEDED(acrylic::SetBarHeight(module, slot, function, 0)) && table[1] == reinterpret_cast<void*>(&StockMinSize) && acrylic::BarHeight() == 0, "stock method restored");
    Check(call().cy == 144, "stock height after restore");

    table[1] = reinterpret_cast<void*>(&Other); // a different Taskbar.dll build
    Check(acrylic::SetBarHeight(module, slot, function, 64) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) && table[1] == reinterpret_cast<void*>(&Other) && acrylic::BarHeight() == 0, "unexpected slot contents are never written");
    table[1] = reinterpret_cast<void*>(&StockMinSize);
    Check(acrylic::SetBarHeight(module, slot + 4, function, 64) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), "misaligned slot rejected");
    Check(acrylic::SetBarHeight(module, 0x7ffffff0, function, 64) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), "slot outside the module rejected");
    Check(acrylic::SetBarHeight(module, 0, 0, 64) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && acrylic::SetBarHeight(nullptr, slot, function, 64) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "nothing to patch without a lookup");
    Check(table[1] == reinterpret_cast<void*>(&StockMinSize) && acrylic::BarHeight() == 0, "rejections change nothing");
}
void Vtable() {
    // A module image: headers, then a three-entry vtable followed by a non-pointer.
    std::vector<BYTE> image(0x3000);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.ImageBase = 0x180000000;
    nt->OptionalHeader.SizeOfImage = 0x3000;
    const ULONGLONG entries[] = {0x180002000, 0x180002100, 0x180002200, 0x1234};
    std::memcpy(image.data() + 0x1000, entries, sizeof(entries));
    Check(acrylic::FindVtableSlot(image.data(), 0x1000, 0x2100) == 0x1008 && acrylic::FindVtableSlot(image.data(), 0x1000, 0x2200) == 0x1010, "slot found by the method's address");
    Check(acrylic::FindVtableSlot(image.data(), 0x1000, 0x2300) == 0, "the table ends at the first value outside the module");
    Check(acrylic::FindVtableSlot(image.data(), 0, 0x2100) == 0 && acrylic::FindVtableSlot(nullptr, 0x1000, 0x2100) == 0, "no table, no slot");
    dos->e_magic = 0;
    Check(acrylic::FindVtableSlot(image.data(), 0x1000, 0x2100) == 0, "not an image");
}
void Key() {
    wchar_t path[MAX_PATH]{};
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\kernel32.dll");
    const auto key = acrylic::ReadSymbolKey(path);
    std::wstring pdb = key.pdb;
    for (auto& c : pdb) c = static_cast<wchar_t>(std::towlower(c));
    bool hex = key.id.size() >= 33;
    for (const auto c : key.id) hex = hex && std::iswxdigit(c);
    Check(pdb == L"kernel32.pdb" && hex, "symbol file name and store id read from the CodeView record");
    Check(acrylic::ReadSymbolKey(L"C:\\nothing-here.dll").id.empty(), "missing module, no key");
}
int wmain(int argc, wchar_t** argv) {
    try {
        Slot();
        Vtable();
        Key();
        if (argc > 1 && !wcscmp(argv[1], L"--live")) {
            acrylic::MinSizeSlot slot;
            const HRESULT result = acrylic::LocateMinSize(slot);
            std::printf("live: result=0x%08lx slot=0x%lx function=0x%lx\n", static_cast<unsigned long>(result), slot.slot, slot.function);
            Check(SUCCEEDED(result) && slot.slot && slot.function, "installed Taskbar.dll's GetMinSize slot");
        }
        std::puts("PASS: one validated vtable slot, stock method first, idempotent height, exact restore, mismatch/alignment/range rejection; vtable scan; CodeView symbol key.");
        return 0;
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}

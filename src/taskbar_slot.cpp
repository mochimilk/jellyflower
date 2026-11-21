// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar_slot.hpp"
#include <dbghelp.h>
#include <shlobj.h>
#include <winhttp.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <thread>
#include <vector>

namespace acrylic {
namespace {
constexpr wchar_t MinSizeName[] = L"?GetMinSize@TrayUI@@UEAAXPEAUHMONITOR__@@PEAUtagSIZE@@@Z";
constexpr wchar_t TableName[] = L"??_7TrayUI@@6BITrayComponentHost@@@";
constexpr wchar_t CacheValue[] = L"MinSizeSlot";
constexpr ULONGLONG MaxSymbols = 64ull << 20;

HRESULT Last() noexcept { const DWORD error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }
// A module mapped as an image: sections at their RVAs, no code run.
struct Image {
    HMODULE module;
    explicit Image(const wchar_t* path) : module(LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {}
    ~Image() { if (module) FreeLibrary(module); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    const BYTE* Bytes() const { return reinterpret_cast<const BYTE*>(reinterpret_cast<ULONG_PTR>(module) & ~ULONG_PTR(3)); }
};
const IMAGE_NT_HEADERS64* Headers(const BYTE* image) noexcept {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? nt : nullptr;
}
std::wstring TaskbarPath() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    return std::wstring(system) + L"\\Taskbar.dll";
}
std::wstring SymbolFolder(const std::wstring& id) {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    std::wstring folder = std::wstring(local) + L"\\jellyflower\\symbols\\" + id;
    CoTaskMemFree(local);
    return folder;
}
// The cache line is "<pdb>/<id> <slot> <function>", hexadecimal RVAs.
bool ReadCache(const SymbolKey& key, MinSizeSlot& slot) {
    wchar_t text[256]{};
    DWORD size = sizeof(text);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", CacheValue, RRF_RT_REG_SZ, nullptr, text, &size)) return false;
    wchar_t name[200]{};
    unsigned slotRva = 0, function = 0;
    if (swscanf_s(text, L"%199s %x %x", name, static_cast<unsigned>(std::size(name)), &slotRva, &function) != 3) return false;
    if (key.pdb + L"/" + key.id != name || !slotRva || !function) return false;
    slot = {slotRva, function};
    return true;
}
void WriteCache(const SymbolKey& key, const MinSizeSlot& slot) {
    wchar_t text[256]{};
    swprintf_s(text, L"%s/%s %x %x", key.pdb.c_str(), key.id.c_str(), slot.slot, slot.function);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\jellyflower", CacheValue, REG_SZ, text, static_cast<DWORD>((wcslen(text) + 1) * sizeof(wchar_t)));
}
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};
// Microsoft's public symbol server; its redirect to storage stays on HTTPS.
HRESULT Download(const std::wstring& path, const std::wstring& file) {
    Internet session(WinHttpOpen(L"jellyflower", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value || !WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 30000)) return Last();
    Internet connection(WinHttpConnect(session.value, L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) return Last();
    Internet request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request.value, nullptr)) return Last();
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) return Last();
    if (status != 200) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, status);
    const HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return Last();
    std::vector<BYTE> buffer(1 << 16);
    ULONGLONG total = 0;
    HRESULT result = S_OK;
    for (DWORD read = 0;; total += read) {
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) { result = Last(); break; }
        if (!read) break;
        DWORD written = 0;
        if (total + read > MaxSymbols) { result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE); break; }
        if (!WriteFile(output, buffer.data(), read, &written, nullptr) || written != read) { result = Last(); break; }
    }
    CloseHandle(output);
    return result;
}
struct Names { DWORD64 base; DWORD table, function; };
BOOL CALLBACK Match(PSYMBOL_INFOW symbol, ULONG, PVOID context) {
    auto& names = *static_cast<Names*>(context);
    // Windows' dbghelp counts the terminator in NameLen.
    const std::wstring_view name(symbol->Name, wcsnlen(symbol->Name, symbol->NameLen));
    if (name == MinSizeName) names.function = static_cast<DWORD>(symbol->Address - names.base);
    else if (name == TableName) names.table = static_cast<DWORD>(symbol->Address - names.base);
    return !(names.table && names.function);
}
// dbghelp is single-threaded; only the lookup worker uses it in the controller.
HRESULT ReadSymbols(const std::wstring& module, const std::wstring& folder, DWORD& table, DWORD& function) {
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitializeW(process, folder.c_str(), FALSE)) return Last();
    Names names{SymLoadModuleExW(process, nullptr, module.c_str(), nullptr, 0x10000000, 0, nullptr, 0), 0, 0};
    IMAGEHLP_MODULEW64 info{sizeof(info)};
    if (names.base && SymGetModuleInfoW64(process, names.base, &info) && info.SymType == SymPdb)
        SymEnumSymbolsW(process, names.base, L"*", Match, &names);
    SymCleanup(process);
    if (!names.table || !names.function) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    table = names.table;
    function = names.function;
    return S_OK;
}
}

SymbolKey ReadSymbolKey(const wchar_t* module) {
    const Image image(module);
    if (!image.module) return {};
    const BYTE* bytes = image.Bytes();
    const auto* nt = Headers(bytes);
    if (!nt) return {};
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    const auto& entry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (entry.VirtualAddress > size || entry.Size > size - entry.VirtualAddress) return {};
    const auto* debug = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(bytes + entry.VirtualAddress);
    for (DWORD i = 0; i < entry.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        const auto& item = debug[i];
        // "RSDS", GUID, age, then the symbol file's path.
        if (item.Type != IMAGE_DEBUG_TYPE_CODEVIEW || item.SizeOfData <= 24 || item.AddressOfRawData > size || item.SizeOfData > size - item.AddressOfRawData) continue;
        const BYTE* record = bytes + item.AddressOfRawData;
        if (std::memcmp(record, "RSDS", 4)) continue;
        GUID guid{};
        DWORD age = 0;
        std::memcpy(&guid, record + 4, sizeof(guid));
        std::memcpy(&age, record + 20, sizeof(age));
        const auto* path = reinterpret_cast<const char*>(record + 24);
        std::string_view name(path, strnlen(path, item.SizeOfData - 24));
        if (const auto slash = name.find_last_of("\\/"); slash != std::string_view::npos) name.remove_prefix(slash + 1);
        if (name.empty()) continue;
        wchar_t id[64]{};
        swprintf_s(id, L"%08lX%04hX%04hX%02X%02X%02X%02X%02X%02X%02X%02X%lX", guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
        return {std::wstring(name.begin(), name.end()), id};
    }
    return {};
}
DWORD FindVtableSlot(const BYTE* image, DWORD table, DWORD function) noexcept {
    const auto* nt = image ? Headers(image) : nullptr;
    if (!nt || !table || !function) return 0;
    const ULONGLONG base = nt->OptionalHeader.ImageBase;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    // Entries are the methods' preferred addresses; the table ends where a
    // value stops pointing into the module.
    for (DWORD offset = table; offset < size - sizeof(ULONGLONG) && offset - table < 512 * sizeof(ULONGLONG); offset += sizeof(ULONGLONG)) {
        ULONGLONG entry = 0;
        std::memcpy(&entry, image + offset, sizeof(entry));
        if (entry < base || entry >= base + size) return 0;
        if (entry - base == function) return offset;
    }
    return 0;
}
bool MinSizeCached() noexcept {
    try {
        MinSizeSlot slot;
        const auto key = ReadSymbolKey(TaskbarPath().c_str());
        return !key.id.empty() && ReadCache(key, slot);
    } catch (...) { return false; }
}
HRESULT LocateMinSize(MinSizeSlot& result) noexcept {
    try {
        const auto module = TaskbarPath();
        const auto key = ReadSymbolKey(module.c_str());
        if (key.id.empty()) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if (ReadCache(key, result)) return S_OK;
        const auto folder = SymbolFolder(key.id);
        if (folder.empty()) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        const int created = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(created);
        const auto file = folder + L"\\" + key.pdb;
        DWORD table = 0, function = 0;
        HRESULT hr = Download(L"/download/symbols/" + key.pdb + L"/" + key.id + L"/" + key.pdb, file);
        if (SUCCEEDED(hr)) hr = ReadSymbols(module, folder, table, function);
        // Only the two numbers are kept.
        DeleteFileW(file.c_str());
        RemoveDirectoryW(folder.c_str());
        if (FAILED(hr)) return hr;
        const Image image(module.c_str());
        const DWORD slot = image.module ? FindVtableSlot(image.Bytes(), table, function) : 0;
        if (!slot) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        result = {slot, function};
        WriteCache(key, result);
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}

struct MinSizeLookup::Shared {
    std::atomic_bool done{false};
    MinSizeSlot slot;
    HRESULT result = E_PENDING;
};
void MinSizeLookup::Start() {
    if (shared_) return;
    shared_ = std::make_shared<Shared>();
    std::thread([shared = shared_] {
        MinSizeSlot slot;
        const HRESULT result = LocateMinSize(slot);
        shared->slot = slot;
        shared->result = result;
        shared->done = true;
    }).detach();
}
bool MinSizeLookup::Ready(MinSizeSlot& slot, HRESULT& result) const {
    if (!shared_ || !shared_->done) return false;
    slot = shared_->slot;
    result = shared_->result;
    return true;
}
}

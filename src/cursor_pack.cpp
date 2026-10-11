// SPDX-License-Identifier: GPL-3.0-or-later
#include "cursor_pack.hpp"
#include "cursors.hpp"
#include "../third_party/miniz/miniz.h"
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace acrylic {
namespace {
namespace fs = std::filesystem;
constexpr ULONGLONG MaxZip = 64 * 1024 * 1024, MaxFile = 4 * 1024 * 1024, MaxTotal = 128 * 1024 * 1024;
struct File { HANDLE value = INVALID_HANDLE_VALUE; ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
ULONGLONG Number(DWORD high, DWORD low) { return (static_cast<ULONGLONG>(high) << 32) | low; }
bool Reparse(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool PackName(const std::wstring& name) {
    return name.rfind(L"pack-", 0) == 0 && name.size() < 80 && name.size() > 5
        && name.find_first_not_of(L"0123456789-", 5) == std::wstring::npos;
}
std::wstring Wide(const std::string& text);
std::vector<std::wstring> Manifest(const std::wstring& directory) {
    std::vector<std::wstring> lines;
    if (directory.empty() || Reparse(directory)) return lines;
    try {
        const auto path = fs::path(directory) / L"active.txt";
        if (Reparse(path) || !fs::is_regular_file(path) || fs::file_size(path) > 4096) return lines;
        std::ifstream input(path, std::ios::binary);
        std::string line;
        while (lines.size() < 4 && std::getline(input, line)) lines.push_back(Wide(line));
    } catch (...) { lines.clear(); }
    return lines;
}
bool Same(const CursorImportResult& a, const CursorImportResult& b) {
    return a.source == b.source && a.size == b.size && a.modified == b.modified;
}
void CleanStaging(const fs::path& folder, const fs::path& directory) noexcept {
    // Only remove our fixed generated files from this new, direct child directory.
    try {
        if (!PackName(folder.filename().wstring()) || Reparse(directory) || Reparse(folder)
            || fs::absolute(folder).lexically_normal().parent_path() != fs::absolute(directory).lexically_normal()) return;
        const auto assets = folder / L"cursor" / L"assets";
        if (Reparse(folder / L"cursor") || Reparse(assets)) return;
        for (const auto& color : CursorColors) {
            const auto path = assets / color.folder;
            if (Reparse(path)) continue;
            for (const auto file : CursorRoleFiles) DeleteFileW((path / file).c_str());
            RemoveDirectoryW(path.c_str());
        }
        RemoveDirectoryW(assets.c_str()); RemoveDirectoryW((folder / L"cursor").c_str()); RemoveDirectoryW(folder.c_str());
    } catch (...) {}
}
void Write(const fs::path& path, const void* bytes, DWORD size) {
    File output{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (output.value == INVALID_HANDLE_VALUE) throw GetLastError();
    DWORD written = 0;
    if (!WriteFile(output.value, bytes, size, &written, nullptr) || written != size) throw static_cast<DWORD>(ERROR_WRITE_FAULT);
}
std::wstring Wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) throw static_cast<DWORD>(ERROR_INVALID_DATA);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::string Utf8(const std::wstring& text) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
}
std::wstring CursorDataFolder() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value))) return {};
    const auto path = (fs::path(value) / L"jellyflower" / L"cursors").wstring();
    CoTaskMemFree(value); return path;
}
std::wstring ActiveCursorFolder(const std::wstring& directory) {
    const auto lines = Manifest(directory);
    if (lines.empty() || !PackName(lines[0])) return {};
    const auto path = fs::path(directory) / lines[0];
    std::error_code error;
    return Reparse(path) || !fs::is_directory(path, error) ? L"" : path.wstring();
}
DWORD VerifyCursorFolder(const std::wstring& folder) {
    for (const auto& color : CursorColors) for (const auto name : CursorRoleFiles) {
        const auto path = fs::path(folder) / L"cursor" / L"assets" / color.folder / name;
        const HCURSOR cursor = LoadCursorFromFileW(path.c_str());
        if (!cursor) { const DWORD error = GetLastError(); return error ? error : ERROR_INVALID_DATA; }
        DestroyCursor(cursor);
    }
    return ERROR_SUCCESS;
}
DWORD ImportCursorArchive(const std::wstring& source, const std::wstring& directory, std::wstring& installed) {
    installed.clear(); fs::path staging;
    try {
        if (directory.empty()) return ERROR_BAD_PATHNAME;
        File lock{CreateMutexW(nullptr, FALSE, L"Local\\jellyflower.CursorImport.v1")};
        if (!lock.value) return GetLastError();
        const auto wait = WaitForSingleObject(lock.value, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return ERROR_BUSY;
        struct Unlock { HANDLE value; ~Unlock() { ReleaseMutex(value); } } unlock{lock.value};
        File input{CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (input.value == INVALID_HANDLE_VALUE) return GetLastError();
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(input.value, &info)) return GetLastError();
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return ERROR_INVALID_DATA;
        const auto size = Number(info.nFileSizeHigh, info.nFileSizeLow);
        if (!size || size > MaxZip) return ERROR_FILE_TOO_LARGE;
        std::vector<BYTE> bytes(static_cast<size_t>(size)); DWORD read = 0;
        if (!ReadFile(input.value, bytes.data(), static_cast<DWORD>(size), &read, nullptr) || read != size) return ERROR_READ_FAULT;
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return ERROR_INVALID_DATA;
        struct Zip { mz_zip_archive* value; ~Zip() { mz_zip_reader_end(value); } } close{&zip};
        if (mz_zip_reader_get_num_files(&zip) > 4096) return ERROR_INVALID_DATA;
        std::array<std::array<int, CursorRoleFiles.size()>, CursorColors.size()> entries;
        for (auto& color : entries) color.fill(-1);
        ULONGLONG total = 0;
        for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&zip); ++index) {
            mz_zip_archive_file_stat entry{};
            if (!mz_zip_reader_file_stat(&zip, index, &entry) || mz_zip_reader_get_filename(&zip, index, nullptr, 0) > sizeof(entry.m_filename))
                return ERROR_INVALID_DATA;
            std::string name(entry.m_filename);
            std::replace(name.begin(), name.end(), '\\', '/');
            if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos || entry.m_is_encrypted
                || ((entry.m_external_attr >> 16) & 0170000) == 0120000) return ERROR_INVALID_DATA;
            std::vector<std::wstring> parts; size_t at = 0;
            while (at < name.size()) {
                const auto end = name.find('/', at);
                const auto part = name.substr(at, end == std::string::npos ? name.size() - at : end - at);
                if (part.empty() || part == "." || part == "..") return ERROR_INVALID_DATA;
                auto wide = Wide(part);
                std::transform(wide.begin(), wide.end(), wide.begin(), [](wchar_t c) -> wchar_t { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c; });
                parts.push_back(wide);
                if (end == std::string::npos) break;
                at = end + 1;
            }
            if (entry.m_is_directory || parts.size() < 4) continue;
            const size_t start = parts.size() - 4;
            if (parts[start] != L"cursor" || parts[start + 1] != L"assets") continue;
            for (size_t color = 0; color < CursorColors.size(); ++color) for (size_t role = 0; role < CursorRoleFiles.size(); ++role)
                if (parts[start + 2] == CursorColors[color].folder && parts[start + 3] == CursorRoleFiles[role]) {
                    if (entries[color][role] != -1 || !entry.m_is_supported || !entry.m_uncomp_size || entry.m_uncomp_size > MaxFile)
                        return ERROR_INVALID_DATA;
                    entries[color][role] = static_cast<int>(index);
                    total += entry.m_uncomp_size;
                    if (total > MaxTotal) return ERROR_FILE_TOO_LARGE;
                }
        }
        for (const auto& color : entries) for (const auto entry : color) if (entry < 0) return ERROR_FILE_NOT_FOUND;
        if (Reparse(directory) || Reparse(fs::path(directory).parent_path())) return ERROR_INVALID_DATA;
        fs::create_directories(directory);
        staging = fs::path(directory) / (L"pack-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId()));
        if (!fs::create_directory(staging)) return ERROR_ALREADY_EXISTS;
        for (size_t color = 0; color < CursorColors.size(); ++color) {
            const auto target = staging / L"cursor" / L"assets" / CursorColors[color].folder;
            fs::create_directories(target);
            for (size_t role = 0; role < CursorRoleFiles.size(); ++role) {
                mz_zip_archive_file_stat entry{};
                mz_zip_reader_file_stat(&zip, entries[color][role], &entry);
                std::vector<BYTE> content(static_cast<size_t>(entry.m_uncomp_size));
                if (!mz_zip_reader_extract_to_mem(&zip, entries[color][role], content.data(), content.size(), 0)) throw static_cast<DWORD>(ERROR_CRC);
                Write(target / CursorRoleFiles[role], content.data(), static_cast<DWORD>(content.size()));
            }
        }
        const auto verified = VerifyCursorFolder(staging.wstring());
        if (verified) throw verified;
        // UTF-8 metadata records our generated folder and the original ZIP name.
        const std::string manifest = Utf8(staging.filename().wstring()) + "\n" + Utf8(fs::path(source).filename().wstring()) + "\n"
            + std::to_string(size) + "\n" + std::to_string(Number(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime)) + "\n";
        const auto pending = fs::path(directory) / (staging.filename().wstring() + L".txt");
        Write(pending, manifest.data(), static_cast<DWORD>(manifest.size()));
        if (!MoveFileExW(pending.c_str(), (fs::path(directory) / L"active.txt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError(); fs::remove(pending); throw error;
        }
        installed = staging.wstring(); return ERROR_SUCCESS;
    } catch (DWORD error) { if (!staging.empty()) CleanStaging(staging, directory); return error; }
    catch (...) { if (!staging.empty()) CleanStaging(staging, directory); return ERROR_INVALID_DATA; }
}
DWORD OpenCursorGuide() {
    try {
        const auto directory = CursorDataFolder();
        if (directory.empty() || Reparse(directory) || Reparse(fs::path(directory).parent_path())) return ERROR_BAD_PATHNAME;
        fs::create_directories(directory);
        const std::wstring guide = L"jellyflower - unlock cursor colors\r\n\r\n"
            L"Cursor Concept 3 HDPi is made by JepriCreations. Get your own full pack here:\r\n"
            L"https://jepricreations.com/products/cursor-concept-3-hdpi\r\n\r\n"
            L"1. Download the complete Cursor Concept 3 HDPi ZIP from your account on the creator's website.\r\n"
            L"2. Copy the original .zip file into this folder (also opened for you):\r\n\r\n" + directory + L"\r\n\r\n"
            L"   You can paste that path into File Explorer's address bar. Keep the file zipped; any .zip filename is fine.\r\n"
            L"3. Leave jellyflower running. Within ten seconds it validates the pack and enables the cursor choices.\r\n"
            L"4. Open jellyflower > cursor and pick a color. Default (system) follows Windows light/dark mode.\r\n\r\n"
            L"Your Windows cursor stays active until you choose a custom cursor. Disable selects Microsoft's standard cursors.\r\n"
            L"The full pack includes light, dark, blueberry, green tea, mint, strawberry, thai tea, vanilla, and yogurt.\r\n"
            L"The free light/dark ZIP is a different pack and will not unlock these choices. Use the full HDPi ZIP linked above.\r\n"
            L"No need to extract files, run install.cmd, or install an INF file. Jellyflower reads only the cursor files.\r\n"
            L"The ZIP and imported cursors stay on your computer, survive jellyflower updates, and are never uploaded.\r\n\r\n"
            L"If import fails, check the original ZIP contains cursor/assets/<color>/arrow.cur and the other cursor files.\r\n"
            L"Replace an incomplete download with a fresh ZIP. A failed import keeps your existing pack and cursor.\r\n\r\n"
            L"Creator: https://jepricreations.com  |  https://www.deviantart.com/jepricreations\r\n"
            L"Cursor files are licensed by their creator and are not bundled with jellyflower.\r\n";
        const auto text = std::string("\xef\xbb\xbf") + Utf8(guide);
        const auto path = fs::path(directory) / L"unlock colors.txt";
        const auto pending = fs::path(directory) / (L"guide-" + std::to_wstring(GetTickCount64()) + L".tmp");
        Write(pending, text.data(), static_cast<DWORD>(text.size()));
        if (!MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { const auto error = GetLastError(); fs::remove(pending); return error; }
        const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (opened <= 32) return static_cast<DWORD>(opened);
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return ERROR_SUCCESS;
    } catch (...) { return ERROR_WRITE_FAULT; }
}
struct CursorPackImporter::State { std::mutex mutex; bool busy = false, pending = false; CursorImportResult result; };
CursorPackImporter::CursorPackImporter(std::wstring directory) : state_(std::make_shared<State>()), directory_(std::move(directory)) {
    const auto lines = Manifest(directory_);
    try { if (lines.size() == 4 && !ActiveCursorFolder(directory_).empty())
        seen_.push_back({0, L"", lines[1], std::stoull(lines[2]), std::stoull(lines[3])}); } catch (...) {}
}
void CursorPackImporter::Scan() {
    if (directory_.empty() || Reparse(directory_)) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->busy || state_->pending) return;
    WIN32_FIND_DATAW file{};
    const HANDLE find = FindFirstFileW((fs::path(directory_) / L"*.zip").c_str(), &file);
    if (find == INVALID_HANDLE_VALUE) return;
    CursorImportResult candidate; bool found = false;
    for (UINT count = 0; count < 128; ++count) {
        CursorImportResult item{0, L"", file.cFileName, Number(file.nFileSizeHigh, file.nFileSizeLow),
            Number(file.ftLastWriteTime.dwHighDateTime, file.ftLastWriteTime.dwLowDateTime)};
        if (!(file.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            && std::none_of(seen_.begin(), seen_.end(), [&](const auto& old) { return Same(old, item); })
            && (!found || item.modified < candidate.modified)) { candidate = item; found = true; }
        if (!FindNextFileW(find, &file)) break;
    }
    FindClose(find); if (!found) return;
    state_->busy = true;
    try {
        const auto state = state_; const auto directory = directory_;
        std::thread([state, directory, candidate]() mutable {
            candidate.error = ImportCursorArchive((fs::path(directory) / candidate.source).wstring(), directory, candidate.folder);
            std::lock_guard<std::mutex> completed(state->mutex);
            state->result = std::move(candidate); state->busy = false; state->pending = true;
        }).detach();
    } catch (...) { state_->busy = false; }
}
bool CursorPackImporter::Poll(CursorImportResult& result) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->pending) return false;
    result = state_->result; state_->pending = false;
    if (result.error != ERROR_SHARING_VIOLATION && result.error != ERROR_BUSY) {
        seen_.erase(std::remove_if(seen_.begin(), seen_.end(), [&](const auto& old) { return old.source == result.source; }), seen_.end());
        seen_.push_back(result);
        return true;
    }
    return false;
}
}

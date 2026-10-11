// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_download.hpp"
#include "shared.hpp"
#include "version.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <array>
#include <vector>
#include <winrt/base.h>
namespace acrylic {
namespace {
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<BYTE> object;
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    bool Create() {
        DWORD size = 0, written = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0
            || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&size), sizeof(size), &written, 0) < 0) return false;
        object.resize(size);
        return BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) >= 0;
    }
};
bool Crack(const std::wstring& url, std::wstring& host, std::wstring& path) {
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS
        || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength || parts.dwPasswordLength) return false;
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    return path.find(L'#') == std::wstring::npos;
}
std::filesystem::path UpdatesFolder() {
    PWSTR local = nullptr;
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local));
    const std::filesystem::path result = std::filesystem::path(local) / L"jellyflower" / L"updates";
    CoTaskMemFree(local); return result;
}
}
bool AllowedUpdateUrl(const std::wstring& url, bool initial) {
    std::wstring host, path;
    if (!Crack(url, host, path)) return false;
    if (!initial) return host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com";
    const std::wstring prefix = L"/" + std::wstring(Repository) + L"/releases/download/v";
    if (host != L"github.com" || path.rfind(prefix, 0) != 0) return false;
    const auto slash = path.find(L'/', prefix.size());
    return slash != std::wstring::npos && NewerVersion(path.substr(prefix.size() - 1, slash - prefix.size() + 1), L"0.0.0")
        && path.substr(slash + 1) == L"jellyflower-setup-" + std::wstring(Architecture) + L".exe";
}
DWORD VerifyUpdateFile(const UpdateResult& release, const std::filesystem::path& file) {
    if (!release.size || release.size > MaximumInstallerSize || release.sha256.size() != 64) return ERROR_INVALID_DATA;
    Handle input(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!input) return GetLastError();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(input.get(), &info)) return GetLastError();
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || ((static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != release.size) return ERROR_INVALID_DATA;
    Hash hash;
    if (!hash.Create()) return ERROR_GEN_FAILURE;
    std::array<BYTE, 32768> bytes{}; DWORD read = 0;
    do {
        if (!ReadFile(input.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) return GetLastError();
        if (read && BCryptHashData(hash.hash, bytes.data(), read, 0) < 0) return ERROR_GEN_FAILURE;
    } while (read);
    std::array<BYTE, 32> digest{};
    if (BCryptFinishHash(hash.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return ERROR_GEN_FAILURE;
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring actual;
    for (const auto byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
    return actual == release.sha256 ? ERROR_SUCCESS : ERROR_CRC;
}
DWORD DownloadUpdate(const UpdateResult& release, const std::filesystem::path& destination, const std::function<bool()>& cancelled) {
    if (!AllowedUpdateUrl(release.download, true) || !release.size || release.size > MaximumInstallerSize || release.sha256.size() != 64) return ERROR_INVALID_DATA;
    if (cancelled()) return ERROR_CANCELLED;
    // CREATE_NEW refuses an existing file; never execute a partially downloaded file.
    Handle output(CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output) return GetLastError();
    const auto transfer = [&]() -> DWORD {
        Internet session(WinHttpOpen((std::wstring(L"jellyflower/") + AppVersion).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 10000, 10000, 10000)) return GetLastError();
        std::wstring url = release.download;
        const auto deadline = GetTickCount64() + 180000;
        for (int hop = 0; hop < 4; ++hop) {
            if (cancelled()) return ERROR_CANCELLED;
            if (!AllowedUpdateUrl(url, hop == 0)) return ERROR_ACCESS_DENIED;
            std::wstring host, path; if (!Crack(url, host, path)) return ERROR_INVALID_DATA;
            Internet connection(WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!connection.value) return GetLastError();
            Internet request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            if (!request.value) return GetLastError();
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))
                || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
                || !WinHttpReceiveResponse(request.value, nullptr)) return GetLastError();
            DWORD status = 0, length = sizeof(status);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) return GetLastError();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                length = 0;
                WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX);
                if (!length || length > 32768 * sizeof(wchar_t)) return ERROR_INVALID_DATA;
                std::vector<wchar_t> location(length / sizeof(wchar_t) + 1);
                if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &length, WINHTTP_NO_HEADER_INDEX)) return GetLastError();
                url = location.data(); continue;
            }
            if (status != 200) return status;
            std::array<BYTE, 32768> bytes{}; DWORD read = 0; ULONGLONG total = 0;
            do {
                if (cancelled()) return ERROR_CANCELLED;
                if (GetTickCount64() > deadline) return ERROR_TIMEOUT;
                if (!WinHttpReadData(request.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read)) return GetLastError();
                total += read;
                if (total > release.size) return ERROR_FILE_TOO_LARGE;
                DWORD written = 0;
                if (read && (!WriteFile(output.get(), bytes.data(), read, &written, nullptr) || written != read)) return ERROR_WRITE_FAULT;
            } while (read);
            if (total != release.size) return ERROR_HANDLE_EOF;
            return FlushFileBuffers(output.get()) ? ERROR_SUCCESS : GetLastError();
        }
        return ERROR_WINHTTP_REDIRECT_FAILED;
    };
    DWORD result = transfer(); output.reset();
    if (!result && !cancelled()) result = VerifyUpdateFile(release, destination);
    if (!result && cancelled()) result = ERROR_CANCELLED;
    if (result) DeleteFileW(destination.c_str());
    return result;
}
std::filesystem::path CreateUpdateFolder() {
    const auto base = UpdatesFolder(); std::filesystem::create_directories(base);
    GUID id{}; winrt::check_hresult(CoCreateGuid(&id));
    wchar_t name[40]{}; StringFromGUID2(id, name, 40);
    const auto folder = base / name;
    if (!std::filesystem::create_directory(folder)) throw std::runtime_error("update folder");
    return folder;
}
void RemoveUpdateDownload(const std::filesystem::path& installer) noexcept {
    try {
        const auto folder = installer.parent_path(), base = UpdatesFolder();
        if (installer.filename() != L"jellyflower-setup.exe" || std::filesystem::weakly_canonical(folder).parent_path() != std::filesystem::weakly_canonical(base)) return;
        std::error_code ignored; std::filesystem::remove(installer, ignored); std::filesystem::remove(folder, ignored);
    } catch (...) {}
}
void CleanupUpdateDownloads() noexcept {
    try {
        const auto base = UpdatesFolder();
        if (!std::filesystem::exists(base)) return;
        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_directory() || entry.is_symlink()) continue;
            // A setup may still be running when the new controller starts.
            // Files locked for execution remain until a later launch.
            RemoveUpdateDownload(entry.path() / L"jellyflower-setup.exe");
        }
    } catch (...) {}
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "updates.hpp"
#include "version.hpp"
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <mutex>
#include <thread>
#include <vector>

namespace acrylic {
namespace {
bool Version(std::wstring text, std::array<unsigned, 3>& numbers) {
    if (!text.empty() && text[0] == L'v') text.erase(0, 1);
    size_t at = 0;
    for (size_t i = 0; i < numbers.size(); ++i) {
        const size_t start = at;
        unsigned value = 0;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') {
            if (value > 6553 || (value == 6553 && text[at] > L'5')) return false;
            value = value * 10 + text[at++] - L'0';
        }
        if (at == start || (at - start > 1 && text[start] == L'0')) return false;
        numbers[i] = value;
        if (i < 2 && (at == text.size() || text[at++] != L'.')) return false;
    }
    return at == text.size();
}
struct Internet {
    HINTERNET handle = nullptr;
    explicit Internet(HINTERNET value) : handle(value) {}
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};
UpdateResult Error(DWORD error) { UpdateResult value; value.error = error; return value; }
}
bool NewerVersion(const std::wstring& candidate, const std::wstring& current) {
    std::array<unsigned, 3> a{}, b{};
    return Version(candidate, a) && Version(current, b) && a > b;
}
UpdateResult ParseRelease(const std::string& json, const std::wstring& current, const std::wstring& architecture) {
    try {
        if (json.empty() || json.size() > 1024 * 1024) return Error(ERROR_INVALID_DATA);
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(), static_cast<int>(json.size()), nullptr, 0);
        if (!count) return Error(ERROR_INVALID_DATA);
        std::wstring wide(static_cast<size_t>(count), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(), static_cast<int>(json.size()), wide.data(), count))
            return Error(ERROR_INVALID_DATA);
        const auto release = winrt::Windows::Data::Json::JsonObject::Parse(wide);
        if (release.GetNamedBoolean(L"draft") || release.GetNamedBoolean(L"prerelease")) return Error(ERROR_INVALID_DATA);
        const std::wstring tag(release.GetNamedString(L"tag_name"));
        std::array<unsigned, 3> parsed{}, installed{};
        if (tag.empty() || tag[0] != L'v' || !Version(tag, parsed) || !Version(current, installed)) return Error(ERROR_INVALID_DATA);
        UpdateResult result;
        result.version = tag.substr(1);
        if (parsed <= installed) { result.status = UpdateStatus::Current; return result; }
        const std::wstring name = L"jellyflower-setup-" + architecture + L".exe";
        const std::wstring expected = std::wstring(ReleasesUrl) + L"/download/" + tag + L"/" + name;
        for (const auto& item : release.GetNamedArray(L"assets")) {
            const auto asset = item.GetObject();
            if (asset.GetNamedString(L"name") == name && asset.GetNamedString(L"state") == L"uploaded"
                && asset.GetNamedNumber(L"size") > 0 && asset.GetNamedString(L"browser_download_url") == expected) {
                result.status = UpdateStatus::Available;
                result.download = expected;
                return result;
            }
        }
        return Error(ERROR_FILE_NOT_FOUND); // A release without this architecture is not offered.
    } catch (...) { return Error(ERROR_INVALID_DATA); }
}
UpdateResult CheckForUpdates() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        Internet session(WinHttpOpen((std::wstring(L"jellyflower/") + AppVersion).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.handle) return Error(GetLastError());
        if (!WinHttpSetTimeouts(session.handle, 3000, 4000, 4000, 4000)) return Error(GetLastError());
        Internet connection(WinHttpConnect(session.handle, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.handle) return Error(GetLastError());
        const auto path = L"/repos/" + std::wstring(Repository) + L"/releases/latest";
        Internet request(WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.handle) return Error(GetLastError());
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) return Error(GetLastError());
        const wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1), nullptr, 0, 0, 0)
            || !WinHttpReceiveResponse(request.handle, nullptr)) return Error(GetLastError());
        DWORD status = 0, length = sizeof(status);
        if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) return Error(GetLastError());
        if (status == 404) { UpdateResult result; result.status = UpdateStatus::NoRelease; return result; }
        if (status != 200) return Error(status); // Includes rate limit and server errors.
        std::string body;
        const auto deadline = GetTickCount64() + 15000;
        char bytes[8192]; DWORD read = 0;
        do {
            if (GetTickCount64() > deadline) return Error(ERROR_TIMEOUT);
            if (!WinHttpReadData(request.handle, bytes, sizeof(bytes), &read)) return Error(GetLastError());
            if (body.size() + read > 1024 * 1024) return Error(ERROR_FILE_TOO_LARGE);
            body.append(bytes, read);
        } while (read);
        return ParseRelease(body, AppVersion, Architecture);
    } catch (...) { return Error(ERROR_INVALID_DATA); }
}
const wchar_t* UpdateStatusLabel(UpdateStatus status) noexcept {
    switch (status) {
    case UpdateStatus::Checking: return L"checking...";
    case UpdateStatus::Current: return L"up to date";
    case UpdateStatus::Available: return L"available";
    case UpdateStatus::NoRelease: return L"no release yet";
    case UpdateStatus::Error: return L"try again";
    default: return L"";
    }
}
struct UpdateChecker::State {
    mutable std::mutex lock;
    UpdateResult result{UpdateStatus::Idle};
    bool pending = false;
};
UpdateChecker::UpdateChecker() : state_(std::make_shared<State>()) {}
void UpdateChecker::Start() {
    const auto state = state_;
    std::lock_guard<std::mutex> lock(state->lock);
    if (state->result.status == UpdateStatus::Checking) return;
    state->result = UpdateResult{UpdateStatus::Checking}; state->pending = false;
    try {
        // The worker owns only its shared result, never a controller or HWND.
        // Exit can tear down the UI immediately while an HTTP request is pending.
        std::thread([state] {
            const auto result = CheckForUpdates();
            std::lock_guard<std::mutex> done(state->lock);
            state->result = result; state->pending = true;
        }).detach();
    } catch (...) { state->result = Error(ERROR_NOT_ENOUGH_MEMORY); state->pending = true; }
}
bool UpdateChecker::Poll(UpdateResult& result) {
    std::lock_guard<std::mutex> lock(state_->lock);
    if (!state_->pending) return false;
    result = state_->result; state_->pending = false; return true;
}
UpdateResult UpdateChecker::Result() const { std::lock_guard<std::mutex> lock(state_->lock); return state_->result; }
UpdateStatus UpdateChecker::Status() const { return Result().status; }
}

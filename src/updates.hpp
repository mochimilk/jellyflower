// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <string>
namespace acrylic {
enum class UpdateStatus : UINT { Idle, Checking, Current, Available, NoRelease, Error };
struct UpdateResult {
    UpdateStatus status = UpdateStatus::Error;
    std::wstring version, download;
    DWORD error = ERROR_SUCCESS;
};
// Strict stable release versions only. Compare numeric components, never text.
bool NewerVersion(const std::wstring& candidate, const std::wstring& current);
UpdateResult ParseRelease(const std::string& json, const std::wstring& current, const std::wstring& architecture);
UpdateResult CheckForUpdates();
const wchar_t* UpdateStatusLabel(UpdateStatus status) noexcept;
class UpdateChecker {
public:
    UpdateChecker();
    void Start();
    bool Poll(UpdateResult& result);
    UpdateStatus Status() const;
    UpdateResult Result() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

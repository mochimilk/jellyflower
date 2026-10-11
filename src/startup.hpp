// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace acrylic {
struct StartupRecord { bool exists = false; std::wstring xml, executable; };
// Storage boundary permits rollback/migration tests without account writes.
struct StartupStore {
    virtual ~StartupStore() = default;
    virtual StartupRecord Read() = 0;
    virtual void Write(const std::wstring& xml) = 0;
    virtual void Remove() = 0;
};
std::wstring LogonTaskXml(const std::wstring& executable, const std::wstring& sid);
bool SameStartupOwner(const StartupRecord& record, const std::wstring& executable);
void RemoveOwnedStartup(StartupStore& store, const std::wstring& executable);
void RestoreStartup(StartupStore& store, const StartupRecord& record);
StartupRecord ReadStartupTask();
void WriteStartupTask(const std::wstring& xml);
void RestoreStartupTask(const StartupRecord& record);
DWORD RegisterStartup(const std::wstring& executable, bool install) noexcept;

// Fast, bounded polling only during shell initialization/recovery. Ordinary
// maintenance stays at five seconds; errors do not cause a permanent busy loop.
inline UINT ShellRetryInterval(ULONGLONG elapsed, bool ready) {
    return !ready && elapsed < 30000 ? 100 : 5000;
}
} // namespace acrylic

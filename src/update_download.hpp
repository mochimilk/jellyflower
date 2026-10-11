// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "updates.hpp"
#include <filesystem>
#include <functional>
namespace acrylic {
inline constexpr ULONGLONG MaximumInstallerSize = 64 * 1024 * 1024;
bool AllowedUpdateUrl(const std::wstring& url, bool initial);
DWORD VerifyUpdateFile(const UpdateResult& release, const std::filesystem::path& file);
DWORD DownloadUpdate(const UpdateResult& release, const std::filesystem::path& destination,
    const std::function<bool()>& cancelled = [] { return false; });
std::filesystem::path CreateUpdateFolder();
void RemoveUpdateDownload(const std::filesystem::path& installer) noexcept;
void CleanupUpdateDownloads() noexcept;
}

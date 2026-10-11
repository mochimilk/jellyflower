// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace acrylic {
inline constexpr std::array<const wchar_t*, 17> CursorRoleFiles{{L"arrow.cur", L"help.cur", L"appstarting.ani", L"wait.ani",
    L"crosshair.cur", L"ibeam.cur", L"nwpen.cur", L"no.cur", L"sizens.cur", L"sizewe.cur", L"sizenwse.cur",
    L"sizenesw.cur", L"sizeall.cur", L"uparrow.cur", L"hand.cur", L"person.cur", L"pin.cur"}};
std::wstring CursorDataFolder();
std::wstring ActiveCursorFolder(const std::wstring& directory);
DWORD VerifyCursorFolder(const std::wstring& folder);
DWORD ImportCursorArchive(const std::wstring& zip, const std::wstring& directory, std::wstring& installed);
DWORD OpenCursorGuide();
struct CursorImportResult { DWORD error = 0; std::wstring folder, source; ULONGLONG size = 0, modified = 0; };
class CursorPackImporter {
public:
    explicit CursorPackImporter(std::wstring directory);
    void Scan();
    bool Poll(CursorImportResult& result);
private:
    struct State;
    std::shared_ptr<State> state_;
    std::wstring directory_;
    std::vector<CursorImportResult> seen_;
};
}

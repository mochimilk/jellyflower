// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/cursor_pack.hpp"
#include <cstdio>
#include <filesystem>
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && wcscmp(argv[1], L"--watch") == 0) {
        namespace fs = std::filesystem;
        fs::create_directories(argv[3]);
        fs::copy_file(argv[2], fs::path(argv[3]) / fs::path(argv[2]).filename(), fs::copy_options::overwrite_existing);
        acrylic::CursorPackImporter watcher(argv[3]); watcher.Scan();
        acrylic::CursorImportResult result;
        const auto deadline = GetTickCount64() + 10000;
        bool completed = false;
        while (GetTickCount64() < deadline) {
            if (watcher.Poll(result)) { completed = true; break; }
            Sleep(20);
        }
        if (!completed) return ERROR_TIMEOUT;
        if (result.error || result.folder != acrylic::ActiveCursorFolder(argv[3])) return ERROR_INVALID_DATA;
        watcher.Scan();
        acrylic::CursorPackImporter restarted(argv[3]); restarted.Scan();
        Sleep(500);
        if (watcher.Poll(result) || restarted.Poll(result)) return ERROR_INVALID_DATA;
        puts("PASS: drop-folder worker imports full ZIP; unchanged archive is skipped before and after restart.");
        return 0;
    }
    if (argc != 3) return ERROR_INVALID_PARAMETER;
    std::wstring installed;
    const auto error = acrylic::ImportCursorArchive(argv[1], argv[2], installed);
    if (!error && (installed != acrylic::ActiveCursorFolder(argv[2]) || acrylic::VerifyCursorFolder(installed))) return ERROR_INVALID_DATA;
    wprintf(L"import result=%lu; folder=%ls\n", error, installed.c_str());
    return static_cast<int>(error);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit opt-in round trip through the real Windows preference adapter.
#include "../src/taskbar_options.hpp"
#include <cstdio>
int wmain(int argc,wchar_t** argv){
    if(argc!=2 || wcscmp(argv[1],L"--roundtrip")){std::puts("Use --roundtrip to test and restore the real taskbar setting.");return 2;}
    const auto original=acrylic::ReadTaskbarOptions();acrylic::TaskbarRecord saved;
    if(original.error || !original.bottom || acrylic::ReadTaskbarRecord(saved)){std::puts("Unsupported/unavailable taskbar; no change.");return 2;}
    const auto changed=acrylic::ToggleTaskbarAutoHide();
    acrylic::TaskbarRecord after;const auto read=acrylic::ReadTaskbarRecord(after);
    const bool ok=!changed.error && changed.autoHide!=original.autoHide && !read && bool(after.data[2]&ABS_AUTOHIDE)==changed.autoHide;
    if(acrylic::ReadTaskbarOptions().autoHide!=original.autoHide)acrylic::ToggleTaskbarAutoHide();
    // A pre-existing live/saved disagreement is restored exactly, too.
    const auto restore=acrylic::PersistTaskbarAutoHide(bool(saved.data[2]&ABS_AUTOHIDE));
    acrylic::TaskbarRecord restored;const auto verified=acrylic::ReadTaskbarRecord(restored);
    const bool same=!restore && !verified && restored.size==saved.size && !memcmp(restored.data.data(),saved.data.data(),saved.size) && acrylic::ReadTaskbarOptions().autoHide==original.autoHide;
    std::printf("%s: native auto-hide changed, saved preference matched, original live/saved states restored; error=%lu.\n",ok&&same?"PASS":"FAIL",changed.error);
    return ok&&same?0:1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/updates.hpp"
#include <winrt/base.h>
#include <iostream>
#include <stdexcept>
using namespace acrylic;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string Release(const std::string& tag, const std::string& architecture = "x64", const std::string& host = "https://github.com/mochimilk/jellyflower") {
    return "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"jellyflower-setup-" + architecture
        + ".exe\",\"state\":\"uploaded\",\"size\":123,\"browser_download_url\":\"" + host + "/releases/download/" + tag + "/jellyflower-setup-" + architecture + ".exe\"}]}";
}
int wmain(int argc, wchar_t** argv) {
    try {
        winrt::init_apartment();
        Check(NewerVersion(L"v0.14.0", L"0.13.0"), "minor update");
        Check(NewerVersion(L"v0.13.10", L"0.13.9"), "numeric ordering");
        Check(!NewerVersion(L"v0.13.0", L"0.13.0"), "equal version");
        Check(!NewerVersion(L"v0.12.9", L"0.13.0"), "no downgrade");
        for (const auto bad : {L"", L"v1", L"v1.2", L"v1.2.3.4", L"v01.2.3", L"v1.2.3-beta", L"v1.2.3/evil", L"v65536.2.3", L"v-1.2.3"})
            Check(!NewerVersion(bad, L"0.13.0"), "invalid version rejected");
        Check(ParseRelease(Release("v0.14.0"), L"0.13.0", L"x64").status == UpdateStatus::Available, "matching installer offered");
        Check(ParseRelease(Release("v0.14.0", "ARM64"), L"0.13.0", L"ARM64").status == UpdateStatus::Available, "native ARM64 installer offered");
        Check(ParseRelease(Release("v0.13.0"), L"0.13.0", L"x64").status == UpdateStatus::Current, "current release");
        Check(ParseRelease(Release("v0.12.0"), L"0.13.0", L"x64").status == UpdateStatus::Current, "older remote release");
        Check(ParseRelease(Release("v0.14.0", "ARM64"), L"0.13.0", L"x64").status == UpdateStatus::Error, "wrong architecture rejected");
        Check(ParseRelease(Release("v0.14.0", "x64", "https://evil.example"), L"0.13.0", L"x64").status == UpdateStatus::Error, "foreign download rejected");
        Check(ParseRelease(Release("v0.14.0", "x64", "https://github.com/other/jellyflower"), L"0.13.0", L"x64").status == UpdateStatus::Error, "foreign repository rejected");
        Check(ParseRelease(Release("v0.14.0-preview"), L"0.13.0", L"x64").status == UpdateStatus::Error, "preview tag rejected");
        for (const auto bad : {"{}", "[]", "null", "not json", "{\"tag_name\":\"v9.0.0\"}"})
            Check(ParseRelease(bad, L"0.13.0", L"x64").status == UpdateStatus::Error, "malformed release rejected");
        auto preview = Release("v0.14.0");
        const std::string flag = "\"prerelease\":false";
        preview.replace(preview.find(flag), flag.size(), "\"prerelease\":true");
        Check(ParseRelease(preview, L"0.13.0", L"x64").status == UpdateStatus::Error, "prerelease rejected");
        Check(ParseRelease(std::string(1024*1024+1,'x'), L"0.13.0", L"x64").status == UpdateStatus::Error, "response size bounded");
        Check(ParseRelease(std::string("\xff"), L"0.13.0", L"x64").status == UpdateStatus::Error, "invalid UTF-8 rejected");
        winrt::uninit_apartment();
        std::cout << "PASS: numeric versions, stable releases, strict asset origin/architecture, malformed/oversized responses.\n";
        if (argc == 2 && wcscmp(argv[1], L"--live") == 0) {
            const auto live = CheckForUpdates();
            std::wcout << L"Live GitHub status: " << UpdateStatusLabel(live.status) << L"; version=" << live.version << L"; error=" << live.error << L"\n";
            return live.status == UpdateStatus::Current || live.status == UpdateStatus::NoRelease ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

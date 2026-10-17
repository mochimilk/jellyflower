// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/updates.hpp"
#include "../src/localization.hpp"
#include "../src/update_download.hpp"
#include <winrt/base.h>
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace acrylic;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string Release(const std::string& tag, const std::string& architecture = "x64", const std::string& host = "https://github.com/mochimilk/jellyflower") {
    return "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"jellyflower-setup-" + architecture
        + ".exe\",\"state\":\"uploaded\",\"size\":123,\"digest\":\"sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"browser_download_url\":\"" + host + "/releases/download/" + tag + "/jellyflower-setup-" + architecture + ".exe\"}]}";
}
int wmain(int argc, wchar_t** argv) { acrylic::SetTextLanguage(acrylic::Language::English);
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        Check(std::wstring(UpdateStatusLabel(UpdateStatus::Available))==L"download", "available update uses download label");
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
        for (const auto url : {L"http://release-assets.githubusercontent.com/file", L"https://evil.example/file", L"https://release-assets.githubusercontent.com.evil.example/file", L"https://user:pass@release-assets.githubusercontent.com/file", L"https://release-assets.githubusercontent.com:444/file"})
            Check(!AllowedUpdateUrl(url,false), "unsafe download redirect rejected");
        Check(AllowedUpdateUrl(L"https://release-assets.githubusercontent.com/github-production-release-asset/test?signature=123",false), "GitHub CDN allowed");
        const auto file=std::filesystem::path(L"update-hash-"+std::to_wstring(GetCurrentProcessId())+L".tmp");
        struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code ignored;std::filesystem::remove(file,ignored);}} cleanup{file};
        auto release=ParseRelease(Release("v0.14.0"),L"0.13.0",L"x64");release.size=3;
        {std::ofstream output(file,std::ios::binary);output<<"abc";}
        Check(VerifyUpdateFile(release,file)==0,"exact SHA256 and byte count accepted");
        release.size=4;Check(VerifyUpdateFile(release,file)==ERROR_INVALID_DATA,"truncated payload rejected");release.size=3;
        {std::ofstream output(file,std::ios::binary);output<<"abd";}
        Check(VerifyUpdateFile(release,file)==ERROR_CRC,"corrupt payload rejected");
        release.download=L"https://evil.example/setup.exe";Check(DownloadUpdate(release,file)==ERROR_INVALID_DATA,"invalid transfer cannot overwrite existing file");
        release.download=L"https://github.com/mochimilk/jellyflower/releases/download/v0.14.1/jellyflower-setup-x64.exe";
        Check(DownloadUpdate(release,file,[]{return true;})==ERROR_CANCELLED,"cancel before transfer leaves installed app untouched");
        auto digest=Release("v0.14.0");digest.replace(digest.find("sha256:"),7,"sha512:");
        Check(ParseRelease(digest,L"0.13.0",L"x64").status==UpdateStatus::Error,"missing or unsupported digest rejected");
        std::cout << "PASS: numeric versions, stable releases, strict asset origin/architecture, malformed/oversized responses.\n";
        if (argc == 2 && wcscmp(argv[1], L"--live") == 0) {
            const auto live = CheckForUpdates();
            std::wcout << L"Live GitHub status: " << UpdateStatusLabel(live.status) << L"; version=" << live.version << L"; error=" << live.error << L"\n";
            return live.status == UpdateStatus::Current || live.status == UpdateStatus::NoRelease ? 0 : 1;
        }
        if (argc == 3 && wcscmp(argv[1], L"--download-live") == 0) {
            // Exercise production metadata, redirects, transfer and digest
            // without launching or installing anything.
            auto live=CheckForUpdates();
            if (live.version.empty()) return 1;
            const std::string tag=winrt::to_string(live.version);
            // Current release is not offered by CheckForUpdates; fetch the
            // actual asset specification through a test-only old-version call.
            const auto metadata=std::filesystem::path(argv[2]);
            std::ifstream source(metadata,std::ios::binary);std::string json((std::istreambuf_iterator<char>(source)),{});
            live=ParseRelease(json,L"0.0.0",sizeof(void*)==8?L"x64":L"");
            const auto output=metadata.parent_path()/L"native-downloaded-setup.exe";
            const DWORD error=DownloadUpdate(live,output);
            std::wcout<<L"Native download and verification: "<<error<<L"; bytes="<<live.size<<L"\n";
            return error?1:0;
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

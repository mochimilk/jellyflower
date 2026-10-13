// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the production policy/worker and file resources without setting a desktop wallpaper.
#include "../src/wallpaper.cpp"
#include <iostream>
#include <stdexcept>
using namespace acrylic;
namespace {
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Fake : WallpaperBackend {
    WallpaperState saved;
    WallpaperDesktop desktop{{{L"left", L"original-left.jpg", L""}, {L"right", L"original-right.png", L""}}, 2};
    bool failSave = false, failCapture = false, failApply = false, failRestore = false;
    int applies = 0, restores = 0, captures = 0, saves = 0;
    DWORD applyDelay = 0;
    HRESULT Load(WallpaperState& value) override { value = saved; return S_OK; }
    HRESULT Save(const WallpaperState& value) override { ++saves; if (failSave) return E_ACCESSDENIED; saved = value; return S_OK; }
    HRESULT Capture(WallpaperDesktop& value, bool backup) override {
        ++captures; if (failCapture) return E_ACCESSDENIED; value = desktop;
        if (backup) for (auto& monitor : value.monitors) if (!monitor.image.empty()) monitor.backup = monitor.image + L".backup";
        return S_OK;
    }
    HRESULT Assets(std::wstring& light, std::wstring& dark) override { light = L"jelly-light.png"; dark = L"jelly-dark.png"; return S_OK; }
    HRESULT Apply(const std::wstring& image) override {
        ++applies; Check(saved.enabled, "recovery record must be durable before the OS change");
        if (applyDelay) Sleep(applyDelay);
        desktop.position = 4;
        for (auto& monitor : desktop.monitors) { monitor.image = image; if (failApply) return E_FAIL; }
        return S_OK;
    }
    HRESULT Restore(const WallpaperDesktop& previous, const std::vector<std::wstring>& ids, bool position) override {
        ++restores; if (failRestore) return E_FAIL;
        for (const auto& id : ids) for (const auto& before : previous.monitors) if (id == before.id)
            for (auto& monitor : desktop.monitors) if (id == monitor.id) monitor.image = before.image;
        if (position) desktop.position = previous.position;
        return S_OK;
    }
};
void Policy() {
    Fake fake; WallpaperEngine app(fake); Check(SUCCEEDED(app.Load()) && !app.Enabled(), "wallpaper starts off");
    Check(SUCCEEDED(app.Update(false, true)) && !fake.captures && !fake.applies, "off does no OS or asset work");
    Check(SUCCEEDED(app.Update(true, true)) && app.Enabled() && fake.applies == 1, "explicit enable applies light");
    Check(fake.desktop.monitors[0].image == L"jelly-light.png" && fake.desktop.position == 4, "light image fills screens");
    Check(SUCCEEDED(app.Update(true, true)) && fake.applies == 1, "unchanged theme does not reset wallpaper");
    Check(SUCCEEDED(app.Update(true, false)) && fake.applies == 2 && fake.desktop.monitors[1].image == L"jelly-dark.png", "dark theme changes both screens");
    Check(fake.saved.previous.monitors[0].image == L"original-left.jpg", "theme updates preserve the original backup");
    WallpaperEngine restarted(fake); Check(SUCCEEDED(restarted.Load()) && restarted.Enabled(), "enabled survives restart/update");
    Check(SUCCEEDED(restarted.Update(false, false)) && !restarted.Enabled() && !fake.saved.enabled, "disable persists off");
    Check(fake.desktop.monitors[0].image == L"original-left.jpg" && fake.desktop.monitors[1].image == L"original-right.png" && fake.desktop.position == 2, "distinct originals and placement restored");

    Fake external; WallpaperEngine observer(external); observer.Load(); observer.Update(true, false);
    external.desktop.monitors[1].image = L"user-new-choice.png";
    Check(SUCCEEDED(observer.Update(true, true)) && !observer.Enabled(), "outside wallpaper changes stop theme following");
    Check(external.desktop.monitors[1].image == L"user-new-choice.png" && external.desktop.monitors[0].image == L"original-left.jpg", "outside choice preserved, owned screen restored");
    Check(external.applies == 1, "outside change never fought by reapplying");

    Fake screens; WallpaperEngine hotplug(screens); hotplug.Load(); hotplug.Update(true, false);
    screens.desktop.monitors.push_back({L"new", L"new-original.jpg", L""});
    Check(SUCCEEDED(hotplug.Update(true, false)) && screens.saved.previous.monitors.size() == 3, "new screen backed up before applying");
    hotplug.Update(false, false); Check(screens.desktop.monitors[2].image == L"new-original.jpg", "new screen original restored");

    Fake solid; solid.desktop.monitors[0].image.clear(); WallpaperEngine blank(solid); blank.Load(); blank.Update(true, true); blank.Update(false, false);
    Check(solid.desktop.monitors[0].image.empty(), "solid-color desktop restored without inventing an image");
}
void Failures() {
    Fake save; save.failSave = true; WallpaperEngine app(save); app.Load();
    Check(FAILED(app.Update(true, true)) && !save.applies && !app.Enabled(), "save failure cannot change wallpaper");
    Fake capture; capture.failCapture = true; WallpaperEngine noBackup(capture); noBackup.Load();
    Check(FAILED(noBackup.Update(true, false)) && !capture.saves && !capture.applies, "unreadable backup prevents apply");
    Fake partial; partial.failApply = true; WallpaperEngine rollback(partial); rollback.Load();
    Check(FAILED(rollback.Update(true, true)) && !rollback.Enabled() && partial.desktop.position == 2
        && partial.desktop.monitors[0].image == L"original-left.jpg", "partial apply failure rolls back screens and position");
    Fake failure; failure.failApply = failure.failRestore = true; WallpaperEngine retained(failure); retained.Load();
    Check(FAILED(retained.Update(true, false)) && retained.Enabled() && failure.saved.enabled, "failed rollback retains durable recovery record");
    failure.failApply = failure.failRestore = false; WallpaperEngine recovery(failure); recovery.Load();
    Check(SUCCEEDED(recovery.Update(false, true)) && !recovery.Enabled() && failure.desktop.position == 2, "restart can finish the interrupted rollback");
    Fake restore; WallpaperEngine retry(restore); retry.Load(); retry.Update(true, false); restore.failRestore = true;
    Check(FAILED(retry.Update(false, false)) && retry.Enabled(), "failed disable retains backup for retry");
    restore.failRestore = false; Check(SUCCEEDED(retry.Update(false, false)) && !retry.Enabled(), "disable can retry successfully");
}
void StorageAndAssets(const fs::path& output, const fs::path& assets) {
    NativeWallpaperBackend files(output); WallpaperState absent;
    Check(SUCCEEDED(files.Load(absent)) && !absent.enabled, "new account storage defaults off");
    WallpaperState original{true, {{{L"monitor", L"C:\\original image.png", L"C:\\backup.png"}}, 4}};
    Check(SUCCEEDED(files.Save(original)), "production atomic state write");
    WallpaperState loaded; Check(SUCCEEDED(files.Load(loaded)) && loaded.enabled && loaded.previous.monitors[0].image == original.previous.monitors[0].image, "persistent state round trip");
    const auto encoded = Encode(original);
    for (size_t size = 0; size < encoded.size(); ++size) {
        std::istringstream truncated(encoded.substr(0, size)); bool rejected = false;
        try { Decode(truncated); } catch (...) { rejected = true; }
        Check(rejected, "every truncated state rejected");
    }
    std::string invalid = encoded; invalid[0] = 0; std::istringstream corrupt(invalid);
    bool rejected = false; try { Decode(corrupt); } catch (...) { rejected = true; }
    Check(rejected, "wrong format cannot discard backups");
    std::wstring light, dark; Check(SUCCEEDED(files.Assets(light, dark)), "embedded PNG extraction");
    Check(ReadBytes(light) == ReadBytes(assets / L"jelly_wall_light.png") && ReadBytes(dark) == ReadBytes(assets / L"jelly_wall_dark.png"), "both extracted assets exactly match supplied originals");
    const auto stamp = fs::last_write_time(light); Check(SUCCEEDED(files.Assets(light, dark)) && fs::last_write_time(light) == stamp, "assets reused without rewriting");
    NativeWallpaperBackend reload(output); WriteAtomic(light, "damaged");
    Check(SUCCEEDED(reload.Assets(light, dark)) && ReadBytes(light) == ReadBytes(assets / L"jelly_wall_light.png"), "damaged extracted asset repaired on restart");
    Check(SUCCEEDED(files.Save({})) && SUCCEEDED(files.Load(loaded)) && !loaded.enabled, "off state persists");
}
void Worker() {
    auto fake = std::make_unique<Fake>(); auto* inspect = fake.get(); inspect->applyDelay = 100;
    WallpaperManager app(nullptr, true, std::move(fake));
    const auto before = GetTickCount64(); app.Toggle(true);
    Check(GetTickCount64() - before < 50 && WallpaperBusy(app.Status()), "slow wallpaper apply returns to the menu immediately");
    app.Refresh(false); app.Refresh(true); app.Refresh(false);
    const auto deadline = GetTickCount64() + 3000;
    while (WallpaperBusy(app.Status()) && GetTickCount64() < deadline) Sleep(5);
    Check(app.Status() == WallpaperStatus::On && inspect->desktop.monitors[0].image == L"jelly-dark.png", "queued theme changes converge on the latest request");
    const auto applies = inspect->applies; Sleep(100);
    Check(inspect->applies == applies, "idle worker performs no polling work");
    app.Toggle(false);
    while (WallpaperBusy(app.Status()) && GetTickCount64() < deadline) Sleep(5);
    Check(app.Status() == WallpaperStatus::Off && inspect->desktop.monitors[0].image == L"original-left.jpg", "async disable restores desktop");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 3, "pass a fresh workspace output directory and source asset directory");
        Policy(); Failures(); StorageAndAssets(argv[1], argv[2]); Worker();
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "read-only COM initialization");
        { NativeWallpaperBackend readOnly(argv[1]); WallpaperDesktop current;
            const HRESULT result = readOnly.Capture(current, false);
            std::cout << "Read-only desktop capture HRESULT=" << std::hex << static_cast<ULONG>(result) << std::dec << " monitors=" << current.monitors.size() << '\n';
            Check(SUCCEEDED(result) && !current.monitors.empty(), "real Windows per-monitor read works; no desktop setter invoked"); }
        CoUninitialize();
        std::cout << "PASS: wallpaper defaults, themes, monitor backup/restore, restart, external changes, failures, atomic storage, exact PNGs, nonblocking/coalesced worker and read-only Windows API.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    catch (...) { std::cerr << "wallpaper check failed\n"; return 1; }
}

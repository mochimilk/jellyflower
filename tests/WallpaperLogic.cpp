// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the production policy/worker and file resources without setting a desktop wallpaper.
#include "../src/wallpaper.cpp"
#include <iostream>
#include <stdexcept>
#include <wrl/implements.h>
using namespace acrylic;
namespace {
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
class DesktopFixture:public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IDesktopWallpaper> {
    static HRESULT Copy(const std::wstring& text,PWSTR* out){*out=static_cast<PWSTR>(CoTaskMemAlloc((text.size()+1)*sizeof(wchar_t)));if(!*out)return E_OUTOFMEMORY;memcpy(*out,text.c_str(),(text.size()+1)*sizeof(wchar_t));return S_OK;}
public:
    std::wstring active,detached,common;
    bool attached=true,missingDetached=false,detachedAttached=false;
    HRESULT readError=S_OK,setError=S_OK,activeRectError=S_OK,detachedRectError=S_OK;
    unsigned sets=0,reads=0,setAttempts=0;
    DESKTOP_WALLPAPER_POSITION position=DWPOS_CENTER;
    DesktopFixture(std::wstring image,std::wstring old):active(std::move(image)),detached(std::move(old)){}
    HRESULT STDMETHODCALLTYPE SetWallpaper(LPCWSTR id,LPCWSTR image)override{++setAttempts;if(FAILED(setError))return setError;if(!id)return E_INVALIDARG;if(wcscmp(id,L"active")==0)active=image;else if(wcscmp(id,L"detached")==0)detached=image;else return E_INVALIDARG;++sets;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetWallpaper(LPCWSTR id,LPWSTR* image)override{*image=nullptr;if(!id)return common.empty()?S_FALSE:Copy(common,image);if(wcscmp(id,L"active")==0)return Copy(active,image);if(wcscmp(id,L"detached")!=0)return E_INVALIDARG;return missingDetached?HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND):Copy(detached,image);}
    HRESULT STDMETHODCALLTYPE GetMonitorDevicePathAt(UINT index,LPWSTR* id)override{return index<2?Copy(index?L"detached":L"active",id):E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE GetMonitorDevicePathCount(UINT* count)override{*count=2;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetMonitorRECT(LPCWSTR id,RECT* rect)override{*rect={0,0,1920,1080};if(wcscmp(id,L"active")==0)return FAILED(activeRectError)?activeRectError:attached?S_OK:S_FALSE;if(wcscmp(id,L"detached")==0)return FAILED(detachedRectError)?detachedRectError:detachedAttached?S_OK:S_FALSE;return E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE SetPosition(DESKTOP_WALLPAPER_POSITION placement)override{position=placement;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPosition(DESKTOP_WALLPAPER_POSITION* placement)override{++reads;if(FAILED(readError))return readError;*placement=position;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(COLORREF)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(COLORREF*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSlideshow(IShellItemArray*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSlideshow(IShellItemArray**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS,UINT)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS*,UINT*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE AdvanceSlideshow(LPCWSTR,DESKTOP_SLIDESHOW_DIRECTION)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetStatus(DESKTOP_SLIDESHOW_STATE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Enable(BOOL)override{return E_NOTIMPL;}
};
std::wstring fallbackPath;
std::wstring TestCopy(){return fallbackPath;}
ComPtr<IDesktopWallpaper> replacement;
unsigned connections=0;
HRESULT ConnectFixture(IDesktopWallpaper** desktop){++connections;return replacement.CopyTo(desktop);}
HRESULT activeInventoryError=S_OK;
unsigned inventoryQueries=0;
HRESULT TestActive(const wchar_t* id){++inventoryQueries;return FAILED(activeInventoryError)?activeInventoryError:wcscmp(id,L"active")==0?S_OK:S_FALSE;}
void GenericDetachedFailure(const fs::path& output,const fs::path& assets){
    auto desktop=Microsoft::WRL::Make<DesktopFixture>((assets/L"jelly_wall_dark.png").wstring(),(output/L"missing.png").wstring());
    desktop->detachedRectError=E_FAIL;inventoryQueries=0;activeInventoryError=S_OK;
    NativeWallpaperBackend backend(output,desktop,TestCopy,ConnectFixture,TestActive);WallpaperEngine engine(backend);Check(SUCCEEDED(engine.Load()),"generic detached fixture loads");
    Check(SUCCEEDED(engine.Update(true,true)) && desktop->sets==1 && inventoryQueries==2,"E_FAIL detached rectangle is verified absent in active display inventory before enable");
    Check(SUCCEEDED(engine.Update(false,true)) && !engine.Enabled(),"E_FAIL detached rectangle cannot block restoring the active screen");
    desktop->activeRectError=E_FAIL;WallpaperDesktop current;
    Check(SUCCEEDED(backend.Capture(current,false)) && current.monitors[0].attached && !current.monitors[1].attached,"active inventory can also confirm an attached screen after a generic rectangle failure");
    activeInventoryError=E_ACCESSDENIED;const auto calls=desktop->sets;
    Check(engine.Update(true,true)==E_ACCESSDENIED && desktop->sets==calls && !engine.Enabled(),"unavailable active inventory never guesses which display to modify");
    activeInventoryError=S_OK;desktop->activeRectError=E_ACCESSDENIED;inventoryQueries=0;
    Check(backend.Capture(current,false)==E_ACCESSDENIED && !inventoryQueries && std::wstring_view(backend.Stage())==L"check display","only E_FAIL rectangle results use the fallback, with a useful diagnostic stage");
}
void Reconnect(const fs::path& output){
    const HRESULT lost[]{HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE),HRESULT_FROM_WIN32(RPC_S_CALL_FAILED),HRESULT_FROM_WIN32(RPC_S_CALL_FAILED_DNE),RPC_E_DISCONNECTED,CO_E_OBJNOTCONNECTED,RPC_E_SERVER_DIED,RPC_E_SERVER_DIED_DNE};
    for(const auto error:lost){
        auto stale=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");stale->readError=error;
        auto healthy=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");replacement=healthy;connections=0;
        NativeWallpaperBackend backend(output,stale,TestCopy,ConnectFixture);WallpaperDesktop current;
        Check(SUCCEEDED(backend.Capture(current,false)) && connections==1 && stale->reads==1 && healthy->reads==1 && !healthy->sets,"lost COM connection is replaced once for a read-only capture");
        Check(SUCCEEDED(backend.Capture(current,false)) && connections==1,"healthy replacement reused instead of reconnecting on every event");
    }
    auto denied=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");denied->readError=E_ACCESSDENIED;connections=0;NativeWallpaperBackend permission(output,denied,TestCopy,ConnectFixture);WallpaperDesktop current;
    Check(permission.Capture(current,false)==E_ACCESSDENIED && !connections,"non-connection failures are not retried as COM failures");
    auto unavailable=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");unavailable->readError=HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);replacement=unavailable;connections=0;NativeWallpaperBackend bounded(output,unavailable,TestCopy,ConnectFixture);
    Check(bounded.Capture(current,false)==unavailable->readError && connections==1 && unavailable->reads==2,"unavailable server causes only one reconnect attempt per capture");
    auto healthy=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");replacement=healthy;
    Check(SUCCEEDED(bounded.Capture(current,false)) && connections==2,"failed replacement is discarded so next user request can reconnect");
    auto failedSet=Microsoft::WRL::Make<DesktopFixture>(L"original.png",L"old.png");failedSet->setError=RPC_E_DISCONNECTED;connections=0;NativeWallpaperBackend setters(output,failedSet,TestCopy,ConnectFixture);
    Check(setters.Apply(L"jelly.png",{L"active"})==RPC_E_DISCONNECTED && failedSet->setAttempts==1 && !connections && !healthy->sets,"partially completed apply is not automatically replayed");
    Check(SUCCEEDED(setters.Capture(current,false)) && connections==1,"failed setter releases stale proxy for subsequent recovery");
    auto failedRestore=Microsoft::WRL::Make<DesktopFixture>(L"jelly.png",L"old.png");failedRestore->setError=HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);connections=0;NativeWallpaperBackend restore(output,failedRestore,TestCopy,ConnectFixture);
    const WallpaperDesktop previous{{{L"active",L"original.png",L""}},DWPOS_CENTER};
    Check(restore.Restore(previous,{L"active"},true)==failedRestore->setError && failedRestore->setAttempts==1 && !connections,"failed restore returns its error without replaying changes");
    Check(SUCCEEDED(restore.Restore(previous,{L"active"},true)) && connections==1 && healthy->active==L"original.png" && healthy->position==DWPOS_CENTER,"user retry restores through a fresh proxy without restarting jellyflower");
    replacement.Reset();
}
void DetachedDisplays(const fs::path& output,const fs::path& assets){
    for(bool missing:{false,true}){
        const auto original=(assets/L"jelly_wall_dark.png").wstring();const auto stale=(output/L"deleted-original.jpg").wstring();
        auto desktop=Microsoft::WRL::Make<DesktopFixture>(original,stale);desktop->missingDetached=missing;
        NativeWallpaperBackend backend(output/(missing?L"query-fails":L"file-missing"),desktop);WallpaperEngine engine(backend);Check(SUCCEEDED(engine.Load()),"detached fixture starts with native storage off");
        Check(SUCCEEDED(engine.Update(true,true)) && desktop->sets==1 && desktop->detached==stale,"native enable ignores an unreadable detached assignment and sets only the saved active monitor");
        const auto calls=desktop->sets;Check(SUCCEEDED(engine.Update(true,true)) && desktop->sets==calls && engine.Enabled(),"inactive assignment causes neither false external changes nor repeat setting");
        Check(SUCCEEDED(engine.Update(false,true)) && desktop->active==original && desktop->detached==stale && desktop->position==DWPOS_CENTER,"native disable restores active image and placement without altering detached image");
        desktop->attached=false;Check(backend.Apply(original,{L"active"})==HRESULT_FROM_WIN32(ERROR_NOT_READY) && desktop->sets==calls+1 && desktop->position==DWPOS_CENTER,"a display removed after capture is not changed by the apply operation");
    }
    auto absent=Microsoft::WRL::Make<DesktopFixture>((output/L"missing-active.png").wstring(),L"unused.png");NativeWallpaperBackend strict(output/L"active-unreadable",absent,TestCopy);WallpaperEngine protectedOriginal(strict);protectedOriginal.Load();
    Check(protectedOriginal.Update(true,true)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !absent->sets && !protectedOriginal.Enabled(),"unreadable active original still prevents any destructive change");
    fallbackPath=(assets/L"jelly_wall_dark.png").wstring();absent->common=L"different-monitor-source.png";absent->detachedAttached=true;absent->detached=fallbackPath;NativeWallpaperBackend wrong(output/L"wrong-cache",absent,TestCopy);WallpaperEngine separate(wrong);separate.Load();Check(FAILED(separate.Update(true,true)) && !absent->sets,"a different active monitor's common image cannot be substituted");
    absent->common.clear();NativeWallpaperBackend mixed(output/L"mixed-cache",absent,TestCopy);WallpaperEngine multiple(mixed);multiple.Load();Check(FAILED(multiple.Update(true,true)) && !absent->sets,"S_FALSE common copy is rejected with multiple active displays");
    absent->detachedAttached=false;
    NativeWallpaperBackend single(output/L"single-cache",absent,TestCopy);WallpaperEngine singleScreen(single);singleScreen.Load();
    Check(SUCCEEDED(singleScreen.Update(true,true)) && absent->sets==1,"one active display uses the public current-picture copy even when stale assignments make common return S_FALSE");
    Check(SUCCEEDED(singleScreen.Update(false,true)),"single-screen cached copy restores after source removal");
    absent->active=(output/L"missing-active.png").wstring();absent->sets=0;
    absent->common=absent->active;NativeWallpaperBackend cache(output/L"cached-current",absent,TestCopy);WallpaperEngine recovered(cache);recovered.Load();
    Check(SUCCEEDED(recovered.Update(true,true)) && absent->sets==1,"reported common current-picture copy permits safe enable when source was removed");
    WallpaperState saved;cache.Load(saved);Check(ReadBytes(saved.previous.monitors[0].backup)==ReadBytes(fallbackPath) && fs::path(saved.previous.monitors[0].backup).extension()==L".png","fallback copy is preserved byte-for-byte with its actual format");
    Check(SUCCEEDED(recovered.Update(false,true)) && absent->active==saved.previous.monitors[0].backup,"disable uses saved current-picture copy when original remains missing");
    const auto changing=output/L"locked-original.png";WriteAtomic(changing,ReadBytes(fallbackPath));auto lockedOriginal=Microsoft::WRL::Make<DesktopFixture>(changing.wstring(),L"unused.png");NativeWallpaperBackend restored(output/L"restore-locked",lockedOriginal,TestCopy);WallpaperEngine durable(restored);durable.Load();Check(SUCCEEDED(durable.Update(true,true)),"original captured before it becomes locked");
    HANDLE originalFile=CreateFileW(changing.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(originalFile!=INVALID_HANDLE_VALUE,"original temporarily inaccessible for restoration test");const auto restoredResult=durable.Update(false,true);CloseHandle(originalFile);
    Check(SUCCEEDED(restoredResult) && ReadBytes(lockedOriginal->active)==ReadBytes(fallbackPath),"readable saved copy restores even when the original is locked");
    const auto locked=output/L"locked-image.png";WriteAtomic(locked,"test");HANDLE file=CreateFileW(locked.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"locked source created");HRESULT result=S_OK;try{ReadBytes(locked);}catch(HRESULT code){result=code;}CloseHandle(file);Check(result==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION),"real file error is retained instead of mislabeled as missing");
    Check(std::wstring_view(WallpaperLabel(WallpaperStatus::ErrorOff,result))==L"file in use" && std::wstring_view(WallpaperLabel(WallpaperStatus::ErrorOff,E_ACCESSDENIED))==L"not allowed","actionable wallpaper error labels");
}
struct Fake : WallpaperBackend {
    const wchar_t* stage=L"fixture";
    const wchar_t* Stage() const noexcept override{return stage;}
    WallpaperState saved;
    WallpaperDesktop desktop{{{L"left", L"original-left.jpg", L""}, {L"right", L"original-right.png", L""}}, 2};
    bool failSave = false, failCapture = false, failApply = false, failRestore = false;
    int applies = 0, restores = 0, captures = 0, saves = 0;
    DWORD applyDelay = 0;
    HRESULT Load(WallpaperState& value) override { value = saved; return S_OK; }
    HRESULT Save(const WallpaperState& value) override {stage=L"save recovery"; ++saves; if (failSave) return E_ACCESSDENIED; saved = value; return S_OK; }
    HRESULT Capture(WallpaperDesktop& value, bool backup) override {
        stage=L"capture";++captures; if (failCapture) return E_ACCESSDENIED; value = desktop;
        if(backup)value.monitors.erase(std::remove_if(value.monitors.begin(),value.monitors.end(),[](const auto& monitor){return !monitor.attached;}),value.monitors.end());
        if (backup) for (auto& monitor : value.monitors) if (!monitor.image.empty()) monitor.backup = monitor.image + L".backup";
        return S_OK;
    }
    HRESULT Assets(std::wstring& light, std::wstring& dark) override { light = L"jelly-light.png"; dark = L"jelly-dark.png"; return S_OK; }
    HRESULT Apply(const std::wstring& image,const std::vector<std::wstring>& ids) override {
        stage=L"set wallpaper";++applies; Check(saved.enabled, "recovery record must be durable before the OS change");
        if (applyDelay) Sleep(applyDelay);
        desktop.position = 4;
        for(const auto& id:ids)for(auto& monitor:desktop.monitors)if(monitor.id==id){monitor.image=image;if(failApply)return E_FAIL;}
        return S_OK;
    }
    HRESULT Restore(const WallpaperDesktop& previous, const std::vector<std::wstring>& ids, bool position) override {
        stage=L"restore image";++restores; if (failRestore) return E_FAIL;
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
    Fake laptop;laptop.desktop.monitors.push_back({L"detached",L"missing-old-image.jpg",L"",false});WallpaperEngine portable(laptop);portable.Load();
    Check(SUCCEEDED(portable.Update(true,true)) && laptop.saved.previous.monitors.size()==2 && laptop.desktop.monitors.back().image==L"missing-old-image.jpg","detached assignment neither backed up nor overwritten");
    const auto applies=laptop.applies;Check(SUCCEEDED(portable.Update(true,true)) && portable.Enabled() && laptop.applies==applies,"inactive assignment cannot disable theme following or cause redundant apply");
    portable.Update(false,true);Check(laptop.desktop.monitors.back().image==L"missing-old-image.jpg","disable preserves untouched detached assignment");
    Fake legacy;legacy.desktop.monitors[1].attached=false;legacy.saved={true,legacy.desktop};for(auto& monitor:legacy.desktop.monitors)monitor.image=L"jelly-light.png";
    WallpaperEngine old(legacy);old.Load();Check(SUCCEEDED(old.Update(false,true)) && legacy.desktop.monitors[1].image==L"original-right.png","older snapshots still restore queryable jellyflower-owned detached assignments");
}
void Failures() {
    Fake save; save.failSave = true; WallpaperEngine app(save); app.Load();
    Check(FAILED(app.Update(true, true)) && !save.applies && !app.Enabled(), "save failure cannot change wallpaper");
    Fake capture; capture.failCapture = true; WallpaperEngine noBackup(capture); noBackup.Load();
    Check(FAILED(noBackup.Update(true, false)) && !capture.saves && !capture.applies, "unreadable backup prevents apply");
    Fake partial; partial.failApply = true; WallpaperEngine rollback(partial); rollback.Load();
    Check(FAILED(rollback.Update(true, true)) && !rollback.Enabled() && partial.desktop.position == 2
        && partial.desktop.monitors[0].image == L"original-left.jpg", "partial apply failure rolls back screens and position");
    Check(std::wstring_view(rollback.ErrorStage())==L"set wallpaper" && std::wstring_view(partial.Stage())==L"save recovery","failed apply diagnostic survives successful rollback and off-state save");
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
    fs::remove(light);fs::remove(dark);
    Check(SUCCEEDED(files.Assets(light,dark)) && ReadBytes(light)==ReadBytes(assets/L"jelly_wall_light.png") && ReadBytes(dark)==ReadBytes(assets/L"jelly_wall_dark.png"),"removed app images are re-extracted in the same session without downloading anything");
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
int wmain(int argc, wchar_t** argv) { acrylic::SetTextLanguage(acrylic::Language::English);
    try {
        Check(argc == 3, "pass a fresh workspace output directory and source asset directory");
        Policy(); Failures(); StorageAndAssets(argv[1], argv[2]); DetachedDisplays(fs::path(argv[1])/L"detached",argv[2]);GenericDetachedFailure(fs::path(argv[1])/L"generic-detached",argv[2]); Reconnect(fs::path(argv[1])/L"reconnect"); Worker();
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "read-only COM initialization");
        { NativeWallpaperBackend readOnly(argv[1]); WallpaperDesktop current;
            const HRESULT result = readOnly.Capture(current, false);
            std::cout << "Read-only desktop capture HRESULT=" << std::hex << static_cast<ULONG>(result) << std::dec << " monitors=" << current.monitors.size() << '\n';
            Check(SUCCEEDED(result) && !current.monitors.empty(), "real Windows per-monitor read works; no desktop setter invoked"); }
        CoUninitialize();
        std::cout << "PASS: wallpaper defaults, themes, connected/detached backup/restore, guarded current-image recovery, COM reconnection, restart, external changes, failures, atomic storage, exact PNGs, nonblocking/coalesced worker and read-only Windows API.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    catch (...) { std::cerr << "wallpaper check failed\n"; return 1; }
}

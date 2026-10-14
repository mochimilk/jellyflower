// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpaper.hpp"
#include "resource.h"
#include <shlobj.h>
#include <wrl/client.h>
#include <bcrypt.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <algorithm>

namespace acrylic {
namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
void Require(HRESULT result) { if (FAILED(result)) throw result; }
HRESULT LastError() { const auto error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }
void WriteNumber(std::ostream& out, DWORD value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
DWORD ReadNumber(std::istream& in) {
    DWORD value = 0; if (!in.read(reinterpret_cast<char*>(&value), sizeof(value))) throw E_INVALIDARG;
    return value;
}
void WriteString(std::ostream& out, const std::wstring& value) {
    if (value.size() > 32768) throw E_INVALIDARG;
    WriteNumber(out, static_cast<DWORD>(value.size()));
    out.write(reinterpret_cast<const char*>(value.data()), value.size() * sizeof(wchar_t));
}
std::wstring ReadString(std::istream& in) {
    const auto size = ReadNumber(in); if (size > 32768) throw E_INVALIDARG;
    std::wstring value(size, L'\0');
    if (!in.read(reinterpret_cast<char*>(value.data()), size * sizeof(wchar_t)) || value.find(L'\0') != std::wstring::npos) throw E_INVALIDARG;
    return value;
}
std::string Encode(const WallpaperState& state) {
    std::ostringstream out(std::ios::binary);
    WriteNumber(out, 0x4a465701); WriteNumber(out, state.enabled ? 1 : 0);
    WriteNumber(out, state.previous.position);
    if (state.previous.monitors.size() > 64) throw E_INVALIDARG;
    WriteNumber(out, static_cast<DWORD>(state.previous.monitors.size()));
    for (const auto& monitor : state.previous.monitors) { WriteString(out, monitor.id); WriteString(out, monitor.image); WriteString(out, monitor.backup); }
    return out.str();
}
WallpaperState Decode(std::istream& in) {
    if (ReadNumber(in) != 0x4a465701) throw E_INVALIDARG;
    WallpaperState state;
    const DWORD enabled = ReadNumber(in); if (enabled > 1) throw E_INVALIDARG;
    state.enabled = enabled != 0;
    state.previous.position = ReadNumber(in); if (state.previous.position > 5) throw E_INVALIDARG;
    const auto count = ReadNumber(in); if (count > 64 || (state.enabled && !count)) throw E_INVALIDARG;
    for (DWORD i = 0; i < count; ++i) {
        WallpaperMonitor monitor{ReadString(in), ReadString(in), ReadString(in)};
        if (monitor.id.empty()) throw E_INVALIDARG;
        for (const auto& previous : state.previous.monitors) if (_wcsicmp(previous.id.c_str(), monitor.id.c_str()) == 0) throw E_INVALIDARG;
        state.previous.monitors.push_back(std::move(monitor));
    }
    if (in.peek() != std::char_traits<char>::eof()) throw E_INVALIDARG;
    return state;
}
std::string ReadBytes(const fs::path& path) {
    struct File{HANDLE value;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file{
        CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)throw LastError();
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size))throw LastError();
    if(size.QuadPart<0 || size.QuadPart>64*1024*1024)throw HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))throw LastError();
    if(read!=bytes.size())throw HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    return bytes;
}
void WriteAtomic(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    const fs::path temp = path.wstring() + L".tmp";
    const HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw LastError();
    DWORD written = 0;
    HRESULT result = S_OK;
    if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size() || !FlushFileBuffers(file)) result = LastError();
    CloseHandle(file);
    if (SUCCEEDED(result) && !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) result = LastError();
    if (FAILED(result)) { DeleteFileW(temp.c_str()); throw result; }
}
std::wstring Digest(const std::string& bytes) {
    BYTE hash[32]{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
        static_cast<ULONG>(bytes.size()), hash, sizeof(hash)) < 0) throw E_FAIL;
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring value; for (const BYTE byte : hash) { value += hex[byte >> 4]; value += hex[byte & 15]; }
    return value;
}
fs::path DataFolder() {
    PWSTR local = nullptr; Require(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local));
    const fs::path folder = fs::path(local) / L"jellyflower" / L"wallpaper"; CoTaskMemFree(local); return folder;
}
std::wstring SystemWallpaperImage(){
    wchar_t image[MAX_PATH]{};
    return SystemParametersInfoW(SPI_GETDESKWALLPAPER,static_cast<UINT>(std::size(image)),image,0)?std::wstring(image):std::wstring{};
}
HRESULT FileError(const fs::filesystem_error& error){
    if(error.code().category()==std::system_category())return HRESULT_FROM_WIN32(error.code().value());
    if(error.code()==std::errc::permission_denied)return E_ACCESSDENIED;
    if(error.code()==std::errc::no_such_file_or_directory)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    return E_FAIL;
}
bool LostDesktop(HRESULT result){
    return result==HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)
        || result==HRESULT_FROM_WIN32(RPC_S_CALL_FAILED)
        || result==HRESULT_FROM_WIN32(RPC_S_CALL_FAILED_DNE)
        || result==RPC_E_DISCONNECTED || result==CO_E_OBJNOTCONNECTED
        || result==RPC_E_SERVER_DIED || result==RPC_E_SERVER_DIED_DNE;
}
HRESULT CreateDesktop(IDesktopWallpaper** desktop){
    return CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(desktop));
}
class NativeWallpaperBackend : public WallpaperBackend {
    fs::path folder;
    bool assetsReady = false;
    ComPtr<IDesktopWallpaper> desktop;
    std::wstring (*copyPath)();
    HRESULT (*connect)(IDesktopWallpaper**);
    IDesktopWallpaper* Desktop() {
        if (!desktop) Require(connect(desktop.ReleaseAndGetAddressOf()));
        return desktop.Get();
    }
    HRESULT ConnectionResult(HRESULT result){if(LostDesktop(result))desktop.Reset();return result;}
    static std::wstring Take(PWSTR value) { std::wstring text = value ? value : L""; CoTaskMemFree(value); return text; }
public:
    explicit NativeWallpaperBackend(fs::path value,ComPtr<IDesktopWallpaper> api={},std::wstring (*copy)()=SystemWallpaperImage,HRESULT (*create)(IDesktopWallpaper**)=CreateDesktop)
        : folder(std::move(value)),desktop(std::move(api)),copyPath(copy),connect(create) {}
    HRESULT Load(WallpaperState& state) override {
        try {
            const auto path = folder / L"state.bin";
            if (!fs::exists(path)) { state = {}; return S_OK; }
            if (fs::file_size(path) > 8 * 1024 * 1024) return E_INVALIDARG;
            std::ifstream in(path, std::ios::binary); if (!in) return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
            state = Decode(in); return S_OK;
        } catch (HRESULT error) { return error; } catch(const fs::filesystem_error& error){return FileError(error);} catch (...) { return E_FAIL; }
    }
    HRESULT Save(const WallpaperState& state) override {
        try { WriteAtomic(folder / L"state.bin", Encode(state)); return S_OK; }
        catch (HRESULT error) { return error; } catch(const fs::filesystem_error& error){return FileError(error);} catch (...) { return E_FAIL; }
    }
    HRESULT Assets(std::wstring& light, std::wstring& dark) override {
        try {
            light = (folder / L"jelly-light.png").wstring(); dark = (folder / L"jelly-dark.png").wstring();
            if (assetsReady && fs::exists(light) && fs::exists(dark)) return S_OK;
            const int ids[]{IDR_WALLPAPER_LIGHT, IDR_WALLPAPER_DARK};
            std::wstring* paths[]{&light, &dark};
            for (int i = 0; i < 2; ++i) {
                const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(ids[i]), RT_RCDATA);
                if (!resource) return LastError();
                const DWORD size = SizeofResource(nullptr, resource);
                const auto data = LockResource(LoadResource(nullptr, resource)); if (!data || !size) return E_FAIL;
                const std::string bytes(static_cast<const char*>(data), size);
                const fs::path path = folder / (i ? L"jelly-dark.png" : L"jelly-light.png");
                if (!fs::exists(path) || ReadBytes(path) != bytes) WriteAtomic(path, bytes);
                *paths[i] = path.wstring();
            }
            assetsReady = true; return S_OK;
        } catch (HRESULT error) { return error; } catch(const fs::filesystem_error& error){return FileError(error);} catch (...) { return E_FAIL; }
    }
    HRESULT Capture(WallpaperDesktop& value, bool backup) override {
        auto result=CaptureOnce(value,backup);
        if(!LostDesktop(result))return result;
        // Explorer can replace the wallpaper COM server. Retry only this read /
        // backup operation once; a partially completed setter is never replayed.
        desktop.Reset();
        return ConnectionResult(CaptureOnce(value,backup));
    }
    HRESULT CaptureOnce(WallpaperDesktop& value, bool backup) {
        try {
            WallpaperDesktop next;
            DESKTOP_WALLPAPER_POSITION position{}; Require(Desktop()->GetPosition(&position)); next.position = position;
            UINT count = 0; Require(Desktop()->GetMonitorDevicePathCount(&count)); if (!count || count > 64) return E_FAIL;
            for (UINT i = 0; i < count; ++i) {
                PWSTR id = nullptr, image = nullptr;
                Require(Desktop()->GetMonitorDevicePathAt(i, &id)); const auto name = Take(id);
                RECT rect{};const HRESULT connected=Desktop()->GetMonitorRECT(name.c_str(),&rect);Require(connected);
                const bool attached=connected!=S_FALSE;
                // Old display assignments can reference removed files. Do not
                // back them up or alter them when applying to the laptop screen.
                if(backup && !attached)continue;
                const HRESULT picture=Desktop()->GetWallpaper(name.c_str(), &image);
                if(FAILED(picture)){CoTaskMemFree(image);if(!attached)continue;Require(picture);}
                WallpaperMonitor monitor{name, Take(image), L"",attached};
                if (backup && !monitor.image.empty()) {
                    std::string bytes;auto extension=fs::path(monitor.image).extension().wstring();
                    try{bytes=ReadBytes(monitor.image);}catch(HRESULT originalError){
                        // SPI has only one image. Use it only when Windows
                        // confirms that this exact source is the common image.
                        // Never guess a per-monitor or slideshow cache file.
                        PWSTR common=nullptr;const auto result=Desktop()->GetWallpaper(nullptr,&common);const auto source=Take(common);Require(result);
                        if(result!=S_OK || _wcsicmp(source.c_str(),monitor.image.c_str())!=0)throw originalError;
                        const auto cached=copyPath?copyPath():std::wstring{};
                        if(cached.empty() || _wcsicmp(cached.c_str(),monitor.image.c_str())==0)throw originalError;
                        try{bytes=ReadBytes(cached);}catch(...){throw originalError;}
                        extension=fs::path(cached).extension().wstring();
                        if(bytes.size()>=3 && static_cast<unsigned char>(bytes[0])==0xff && static_cast<unsigned char>(bytes[1])==0xd8)extension=L".jpg";
                        else if(bytes.size()>=8 && bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0)extension=L".png";
                        else if(bytes.size()>=2 && bytes.compare(0,2,"BM")==0)extension=L".bmp";
                    }
                    const fs::path path = folder / (L"original-" + Digest(bytes) + extension);
                    if (!fs::exists(path) || ReadBytes(path) != bytes) WriteAtomic(path, bytes);
                    monitor.backup = path.wstring();
                }
                next.monitors.push_back(std::move(monitor));
            }
            if(std::none_of(next.monitors.begin(),next.monitors.end(),[](const auto& monitor){return monitor.attached;}))return HRESULT_FROM_WIN32(ERROR_NOT_READY);
            value = std::move(next); return S_OK;
        } catch (HRESULT error) { return error; } catch(const fs::filesystem_error& error){return FileError(error);} catch (...) { return E_FAIL; }
    }
    HRESULT Apply(const std::wstring& image,const std::vector<std::wstring>& monitors) override {
        try {
            std::vector<std::wstring> connected;
            for(const auto& id:monitors){RECT rect{};const auto result=Desktop()->GetMonitorRECT(id.c_str(),&rect);Require(result);if(result!=S_FALSE)connected.push_back(id);}
            if(connected.empty())return HRESULT_FROM_WIN32(ERROR_NOT_READY);
            Require(Desktop()->SetPosition(DWPOS_FILL));
            for(const auto& id:connected)Require(Desktop()->SetWallpaper(id.c_str(),image.c_str()));
            return S_OK;
        }
        catch (HRESULT error) { return ConnectionResult(error); } catch (...) { return E_FAIL; }
    }
    HRESULT Restore(const WallpaperDesktop& previous, const std::vector<std::wstring>& monitors, bool position) override {
        try {
            for (const auto& id : monitors) for (const auto& monitor : previous.monitors) if (_wcsicmp(id.c_str(), monitor.id.c_str()) == 0) {
                // Windows can replace its cached source image. Prefer the
                // untouched original file only when its bytes still match.
                std::wstring image = monitor.image;
                if(!monitor.backup.empty()){
                    const auto saved=ReadBytes(monitor.backup);bool matches=false;
                    if(!image.empty())try{matches=ReadBytes(image)==saved;}catch(...){/* Use the preserved copy when the original is inaccessible. */}
                    if(!matches)image=monitor.backup;
                }
                Require(Desktop()->SetWallpaper(id.c_str(), image.c_str()));
            }
            if (position) Require(Desktop()->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(previous.position)));
            return S_OK;
        } catch (HRESULT error) { return ConnectionResult(error); } catch(const fs::filesystem_error& error){return FileError(error);} catch (...) { return E_FAIL; }
    }
};
} // namespace

struct WallpaperManager::Impl {
    HWND owner;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, pending = false, desired = false, light = false;
    UINT64 generation = 0;
    std::atomic<WallpaperStatus> status{WallpaperStatus::Off};
    std::atomic<HRESULT> error{S_OK};
    std::unique_ptr<WallpaperBackend> injected;
    std::thread worker;
    Impl(HWND value, bool theme, std::unique_ptr<WallpaperBackend> backend)
        : owner(value), light(theme), injected(std::move(backend)), worker([this] { Run(); }) {}
    ~Impl() { { std::lock_guard lock(mutex); stopping = true; } wake.notify_one(); worker.join(); }
    void Run() {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try {
            Require(apartment);
            auto backend = injected ? std::move(injected) : std::make_unique<NativeWallpaperBackend>(DataFolder());
            WallpaperEngine engine(*backend);
            HRESULT result = engine.Load();
            bool loaded = SUCCEEDED(result);
            {
                std::lock_guard lock(mutex);
                if (!generation) { desired = engine.Enabled(); pending = SUCCEEDED(result) && desired; }
                if (FAILED(result)) { error = result; status = WallpaperStatus::ErrorOff; }
                else if (!pending) status = engine.Enabled() ? WallpaperStatus::On : WallpaperStatus::Off;
            }
            PostMessageW(owner, WallpaperChangedMessage, 0, 0);
            for (;;) {
                std::unique_lock lock(mutex); wake.wait(lock, [&] { return stopping || pending; });
                if (stopping) break;
                const bool enable = desired, theme = light; const auto request = generation;
                pending = false; status = enable ? WallpaperStatus::Applying : WallpaperStatus::Restoring; lock.unlock();
                PostMessageW(owner, WallpaperChangedMessage, 0, 0);
                if (!loaded) { result = engine.Load(); loaded = SUCCEEDED(result); }
                if (loaded) result = engine.Update(enable, theme);
                lock.lock();
                if (request == generation) {
                    desired = engine.Enabled(); error = result;
                    status = FAILED(result) ? (desired ? WallpaperStatus::ErrorOn : WallpaperStatus::ErrorOff)
                        : desired ? WallpaperStatus::On : WallpaperStatus::Off;
                }
                lock.unlock(); PostMessageW(owner, WallpaperChangedMessage, 0, 0);
            }
        } catch (HRESULT value) { error = value; status = WallpaperStatus::ErrorOff; PostMessageW(owner, WallpaperChangedMessage, 0, 0); }
        catch (...) { error = E_FAIL; status = WallpaperStatus::ErrorOff; PostMessageW(owner, WallpaperChangedMessage, 0, 0); }
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
};
WallpaperManager::WallpaperManager(HWND owner, bool light, std::unique_ptr<WallpaperBackend> backend)
    : impl(std::make_unique<Impl>(owner, light, std::move(backend))) {}
WallpaperManager::~WallpaperManager() = default;
WallpaperStatus WallpaperManager::Status() const { return impl->status.load(); }
HRESULT WallpaperManager::Error() const { return impl->error.load(); }
void WallpaperManager::Toggle(bool light) {
    std::lock_guard lock(impl->mutex);
    if (WallpaperBusy(impl->status)) return;
    // A failed enable retries enabling; a failed disable retries restoring.
    if (SUCCEEDED(impl->error)) impl->desired = !impl->desired;
    else if (!impl->desired) impl->desired = true;
    else impl->desired = false;
    impl->light = light; impl->pending = true; ++impl->generation; impl->error = S_OK;
    impl->status = impl->desired ? WallpaperStatus::Applying : WallpaperStatus::Restoring;
    impl->wake.notify_one();
}
void WallpaperManager::Refresh(bool light) {
    std::lock_guard lock(impl->mutex);
    if (!impl->desired) { impl->light = light; return; }
    if (impl->light == light && WallpaperBusy(impl->status)) return;
    impl->light = light; impl->pending = true; ++impl->generation; impl->wake.notify_one();
}
} // namespace acrylic

// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup.hpp"
#include "shared.hpp"
#include <taskschd.h>
#include <sddl.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <cwchar>

namespace acrylic {
namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT result) { if (FAILED(result)) throw static_cast<DWORD>(HRESULT_CODE(result)); }
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Apartment() { if (result != RPC_E_CHANGED_MODE) Check(result); }
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Bstr {
    BSTR value = nullptr;
    explicit Bstr(const std::wstring& text) : value(SysAllocString(text.c_str())) {
        if (!value) throw static_cast<DWORD>(ERROR_OUTOFMEMORY);
    }
    Bstr() = default;
    ~Bstr() { SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    operator BSTR() const { return value; }
    std::wstring Text() const { return value ? std::wstring(value, SysStringLen(value)) : std::wstring{}; }
};
std::wstring UserSid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) throw GetLastError();
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (!size) throw GetLastError();
    std::vector<BYTE> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size)) throw GetLastError();
    PWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &text)) throw GetLastError();
    const std::wstring sid(text); LocalFree(text); return sid;
}
bool Missing(HRESULT result) { return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND); }
class WindowsStartupStore final : public StartupStore {
    Apartment apartment_;
    std::wstring sid_ = UserSid(), name_ = L"jellyflower-" + sid_;
    ComPtr<ITaskService> service_;
    ComPtr<ITaskFolder> root_;
public:
    WindowsStartupStore() {
        Check(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service_)));
        VARIANT empty{};
        Check(service_->Connect(empty, empty, empty, empty));
        Bstr root(L"\\"); Check(service_->GetFolder(root, &root_));
    }
    const std::wstring& Sid() const { return sid_; }
    StartupRecord Read() override {
        ComPtr<IRegisteredTask> task; Bstr name(name_);
        const HRESULT result = root_->GetTask(name, &task);
        if (Missing(result)) return {};
        Check(result);
        Bstr xml, path;
        Check(task->get_Xml(&xml.value));
        ComPtr<ITaskDefinition> definition; Check(task->get_Definition(&definition));
        ComPtr<IActionCollection> actions; Check(definition->get_Actions(&actions));
        LONG count = 0; Check(actions->get_Count(&count));
        if (count == 1) {
            ComPtr<IAction> action; ComPtr<IExecAction> exec;
            Check(actions->get_Item(1, &action));
            if (SUCCEEDED(action.As(&exec))) Check(exec->get_Path(&path.value));
        }
        return {true, xml.Text(), path.Text()};
    }
    void Write(const std::wstring& text) override {
        Bstr name(name_), xml(text), sid(sid_);
        VARIANT user{}; user.vt = VT_BSTR; user.bstrVal = sid.value;
        VARIANT empty{};
        ComPtr<IRegisteredTask> task;
        Check(root_->RegisterTask(name, xml, TASK_CREATE_OR_UPDATE | TASK_IGNORE_REGISTRATION_TRIGGERS,
            user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &task));
    }
    void Remove() override {
        Bstr name(name_); const HRESULT result = root_->DeleteTask(name, 0);
        if (!Missing(result)) Check(result);
    }
};
std::wstring Xml(const std::wstring& value) {
    std::wstring result;
    for (const wchar_t c : value) {
        switch (c) {
        case L'&': result += L"&amp;"; break;
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'\"': result += L"&quot;"; break;
        case L'\'': result += L"&apos;"; break;
        default: result += c;
        }
    }
    return result;
}
// Read/delete only our exact value. An obsolete uninstaller cannot remove the
// newer executable's startup, and other applications' Run values are untouched.
void RemoveRun(const wchar_t* name, const std::wstring& executable, bool migrating) {
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return;
    if (opened) throw static_cast<DWORD>(opened);
    wchar_t value[32768]{}; DWORD type = 0, size = sizeof(value);
    LONG result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    if (result == ERROR_SUCCESS) {
        value[std::size(value) - 1] = 0;
        const std::wstring command = L"\"" + executable + L"\"";
        const bool owned = type == REG_SZ && (_wcsicmp(value, command.c_str()) == 0
            || (migrating && ((wcscmp(name, RunValue) == 0 && wcsstr(value, L"jellyflower.exe"))
                || (wcscmp(name, L"AcrylicTaskbar") == 0 && wcsstr(value, L"AcrylicTaskbar.exe")))));
        result = owned ? RegDeleteValueW(key, name) : ERROR_ALREADY_EXISTS;
    }
    RegCloseKey(key);
    if (result != ERROR_FILE_NOT_FOUND && result != ERROR_SUCCESS) throw static_cast<DWORD>(result);
}
void WriteRunFallback(const std::wstring& executable) {
    HKEY key = nullptr;
    const auto opened = RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened) throw static_cast<DWORD>(opened);
    const std::wstring command = L"\"" + executable + L"\"";
    const auto result = RegSetValueExW(key, RunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result) throw static_cast<DWORD>(result);
}
} // namespace

std::wstring LogonTaskXml(const std::wstring& executable, const std::wstring& sid) {
    const auto user = Xml(sid), path = Xml(executable), folder = Xml(std::filesystem::path(executable).parent_path().wstring());
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Description>jellyflower: apply the taskbar appearance at sign-in</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + user + L"</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"user\"><UserId>" + user + L"</UserId>"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<AllowHardTerminate>false</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable>"
        L"<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable><RunOnlyIfIdle>false</RunOnlyIfIdle>"
        L"<IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>"
        L"<Enabled>true</Enabled><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>6</Priority></Settings>"
        L"<Actions Context=\"user\"><Exec><Command>" + path + L"</Command><WorkingDirectory>" + folder +
        L"</WorkingDirectory></Exec></Actions></Task>";
}
bool SameStartupOwner(const StartupRecord& record, const std::wstring& executable) {
    return !record.exists || _wcsicmp(record.executable.c_str(), executable.c_str()) == 0;
}
void RemoveOwnedStartup(StartupStore& store, const std::wstring& executable) {
    const auto current = store.Read();
    if (!SameStartupOwner(current, executable)) throw static_cast<DWORD>(ERROR_ALREADY_EXISTS);
    if (current.exists) store.Remove();
}
void RestoreStartup(StartupStore& store, const StartupRecord& record) {
    if (record.exists) store.Write(record.xml); else store.Remove();
}
StartupRecord ReadStartupTask() { WindowsStartupStore store; return store.Read(); }
void WriteStartupTask(const std::wstring& xml) { WindowsStartupStore store; store.Write(xml); }
void RestoreStartupTask(const StartupRecord& record) {
    WindowsStartupStore store;
    RestoreStartup(store, record);
}
DWORD RegisterStartup(const std::wstring& executable, bool install) noexcept {
    try {
        if (executable.empty()) return ERROR_BAD_PATHNAME;
        WindowsStartupStore store;
        if (install) {
            const auto previous = store.Read();
            try {
                store.Write(LogonTaskXml(executable, store.Sid()));
                // The logon task starts promptly. Keep the updated Run value as
                // a delayed fallback and ownership guard for pre-task uninstallers.
                // The existing per-session mutex makes later duplicate launches exit.
                WriteRunFallback(executable);
                RemoveRun(L"AcrylicTaskbar", executable, true);
            } catch (...) { try { RestoreStartup(store, previous); } catch (...) {} throw; }
        } else {
            // Check task ownership before touching the older Run registration.
            if (!SameStartupOwner(store.Read(), executable)) return ERROR_ALREADY_EXISTS;
            RemoveRun(RunValue, executable, false);
            RemoveOwnedStartup(store, executable);
        }
        return ERROR_SUCCESS;
    } catch (DWORD error) { return error ? error : ERROR_GEN_FAILURE; }
    catch (...) { return ERROR_GEN_FAILURE; }
}
} // namespace acrylic

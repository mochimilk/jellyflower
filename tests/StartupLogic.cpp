// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/startup.hpp"
#include <taskschd.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

using namespace acrylic;
using Microsoft::WRL::ComPtr;
void Require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
void Hr(HRESULT value) { if (FAILED(value)) { std::cerr << "Task Scheduler HRESULT=0x" << std::hex << static_cast<unsigned long>(value) << std::endl; throw std::runtime_error("Task Scheduler definition API failed"); } }
struct MemoryStore : StartupStore {
    StartupRecord value;
    StartupRecord Read() override { return value; }
    void Write(const std::wstring& xml) override { value = {true, xml, L"new.exe"}; }
    void Remove() override { value = {}; }
};
int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } apartment;
    try {
        const auto xml = LogonTaskXml(L"C:\\A & B\\jellyflower.exe", L"S-1-5-21-1-2-3-1001");
        Require(xml.find(L"A &amp; B") != std::wstring::npos, "XML paths must be escaped");
        Require(xml.find(L"<Delay>") == std::wstring::npos, "login trigger has no added delay");
        ComPtr<ITaskService> service;
        Hr(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)));
        VARIANT empty{}; Hr(service->Connect(empty, empty, empty, empty));
        ComPtr<ITaskDefinition> definition; Hr(service->NewTask(0, &definition));
        BSTR text = SysAllocString(xml.c_str()); const HRESULT parsed = definition->put_XmlText(text); SysFreeString(text); Hr(parsed);
        ComPtr<ITaskSettings> settings; Hr(definition->get_Settings(&settings));
        VARIANT_BOOL flag = VARIANT_TRUE;
        Hr(settings->get_DisallowStartIfOnBatteries(&flag)); Require(!flag, "startup permitted on battery");
        Hr(settings->get_StopIfGoingOnBatteries(&flag)); Require(!flag, "battery transition does not stop app");
        Hr(settings->get_RunOnlyIfNetworkAvailable(&flag)); Require(!flag, "network not required");
        Hr(settings->get_RunOnlyIfIdle(&flag)); Require(!flag, "idle not required");
        BSTR limit = nullptr; Hr(settings->get_ExecutionTimeLimit(&limit));
        Require(limit && std::wstring(limit) == L"PT0S", "no three-day/default runtime limit"); SysFreeString(limit);
        ComPtr<IPrincipal> principal; Hr(definition->get_Principal(&principal));
        TASK_RUNLEVEL_TYPE level{}; Hr(principal->get_RunLevel(&level)); Require(level == TASK_RUNLEVEL_LUA, "no elevation");
        TASK_LOGON_TYPE logon{}; Hr(principal->get_LogonType(&logon)); Require(logon == TASK_LOGON_INTERACTIVE_TOKEN, "normal interactive desktop token");
        ComPtr<ITriggerCollection> triggers; Hr(definition->get_Triggers(&triggers));
        LONG count = 0; Hr(triggers->get_Count(&count)); Require(count == 1, "one trigger only");
        ComPtr<ITrigger> trigger; Hr(triggers->get_Item(1, &trigger));
        TASK_TRIGGER_TYPE2 type{}; Hr(trigger->get_Type(&type)); Require(type == TASK_TRIGGER_LOGON, "launch at logon");
        ComPtr<IActionCollection> actions; Hr(definition->get_Actions(&actions));
        ComPtr<IAction> action; Hr(actions->get_Item(1, &action));
        ComPtr<IExecAction> exec; Hr(action.As(&exec));
        BSTR path = nullptr; Hr(exec->get_Path(&path));
        Require(path && std::wstring(path) == L"C:\\A & B\\jellyflower.exe", "executable path survives XML exactly"); SysFreeString(path);
        MemoryStore store; store.value = {true, L"old XML", L"old.exe"};
        bool refused = false;
        try { RemoveOwnedStartup(store, L"new.exe"); } catch (DWORD error) { refused = error == ERROR_ALREADY_EXISTS; }
        Require(refused && store.value.xml == L"old XML", "obsolete uninstaller cannot delete another version's task");
        RemoveOwnedStartup(store, L"OLD.exe"); Require(!store.value.exists, "own task removed case-insensitively");
        RemoveOwnedStartup(store, L"old.exe"); Require(!store.value.exists, "missing task removal is idempotent");
        const StartupRecord previous{true, L"<old task including enabled=false />", L"old.exe"};
        store.Write(L"new task"); RestoreStartup(store, previous);
        Require(store.value.xml == previous.xml, "rollback preserves complete prior task XML including disabled state");
        RestoreStartup(store, {}); Require(!store.value.exists, "failed first install removes its new task");
        for (ULONGLONG ms : {0ULL, 5000ULL, 29999ULL}) Require(ShellRetryInterval(ms, false) == 100, "quick retries before shell ready");
        Require(ShellRetryInterval(0, true) == 5000, "ready shell uses ordinary maintenance");
        Require(ShellRetryInterval(30000, false) == 5000, "fast retries bounded at 30 seconds");
        if (argc == 3 && wcscmp(argv[1], L"--xml") == 0) {
            // Write the production definition for independent XML/schema inspection.
            HANDLE file = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(file != INVALID_HANDLE_VALUE, "create fresh XML fixture");
            const wchar_t bom = 0xfeff; DWORD written = 0;
            WriteFile(file, &bom, sizeof(bom), &written, nullptr);
            WriteFile(file, xml.data(), static_cast<DWORD>(xml.size()*sizeof(wchar_t)), &written, nullptr); CloseHandle(file);
        }
        std::cout << "PASS: production logon definition parsed by Task Scheduler; no delay/elevation/network/idle/battery/runtime restrictions; exact paths, obsolete-owner protection, bounded fast shell retries. No tasks registered.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

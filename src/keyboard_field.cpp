// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboard_field.hpp"
// WIN32_LEAN_AND_MEAN leaves out the OLE declarations UI Automation builds on.
#include <ole2.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace acrylic {
struct KeyboardFieldWatcher::Shared {
    std::mutex lock;
    HWND notify = nullptr;
    bool alive = true, focusChanged = false;
    IUIAutomationElement* focus = nullptr;  // latest focused element, owned
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<LONG> state{static_cast<LONG>(KeyboardFieldState::Unknown)};
    ~Shared() {
        if (focus) focus->Release();
        if (wake) CloseHandle(wake);
    }
    void Post() {
        std::lock_guard<std::mutex> guard(lock);
        if (alive && notify) PostMessageW(notify, KeyboardFieldMessage, 0, 0);
    }
};

namespace {
template<class T> struct Ref {
    T* value = nullptr;
    ~Ref() { if (value) value->Release(); }
    T** put() { if (value) { value->Release(); value = nullptr; } return &value; }
    T* operator->() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};
struct Text { BSTR value = nullptr; ~Text() { SysFreeString(value); } };

// UI Automation calls these on its own threads: record, wake, return.
class Handler final : public IUIAutomationFocusChangedEventHandler, public IUIAutomationPropertyChangedEventHandler, public IUIAutomationEventHandler {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<KeyboardFieldWatcher::Shared> shared_;
public:
    explicit Handler(std::shared_ptr<KeyboardFieldWatcher::Shared> shared) : shared_(std::move(shared)) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG left = --refs_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationFocusChangedEventHandler)) *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationPropertyChangedEventHandler)) *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationEventHandler)) *out = static_cast<IUIAutomationEventHandler*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        if (!sender) return S_OK;
        std::lock_guard<std::mutex> guard(shared_->lock);
        sender->AddRef();
        if (shared_->focus) shared_->focus->Release();
        shared_->focus = sender;
        shared_->focusChanged = true;
        SetEvent(shared_->wake);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID, VARIANT) override { SetEvent(shared_->wake); return S_OK; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID) override { SetEvent(shared_->wake); return S_OK; }
};

// Focus that says nothing about where the user types: the touch keyboard's
// own host and jellyflower itself. The tracked field stays as it was.
bool Ignored(IUIAutomationElement* element, int& cachedPid, bool& cachedIgnored) {
    int pid = 0;
    if (FAILED(element->get_CurrentProcessId(&pid))) return false;
    if (static_cast<DWORD>(pid) == GetCurrentProcessId()) return true;
    if (pid == cachedPid) return cachedIgnored;
    bool ignored = false;
    if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid))) {
        wchar_t path[MAX_PATH]{};
        DWORD length = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &length)) {
            const wchar_t* name = wcsrchr(path, L'\\');
            ignored = !_wcsicmp(name ? name + 1 : path, L"TextInputHost.exe");
        }
        CloseHandle(process);
    }
    cachedPid = pid;
    cachedIgnored = ignored;
    return ignored;
}
// A text box whose text can be read: an edit control, not a password box.
bool Readable(IUIAutomationElement* element) {
    CONTROLTYPEID type = 0;
    BOOL password = FALSE;
    if (FAILED(element->get_CurrentControlType(&type)) || type != UIA_EditControlTypeId) return false;
    if (SUCCEEDED(element->get_CurrentIsPassword(&password)) && password) return false;
    return true;
}
// WCAG relative luminance of a UI Automation color (0x00BBGGRR).
double Luminance(LONG color) {
    const auto channel = [](int value) { const double c = value / 255.0; return c <= .03928 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
    return .2126 * channel(color & 0xFF) + .7152 * channel((color >> 8) & 0xFF) + .0722 * channel((color >> 16) & 0xFF);
}
// Chromium and Electron apps report an empty field's placeholder as its text,
// drawn faint (VS Code's chat box: #555555 on #202122, 2.2:1). Text under 3:1
// against its own background is taken as a placeholder; real text in such
// boxes sits near 10:1. Mixed or unreported colors are not a placeholder.
bool Placeholder(IUIAutomationElement* element) {
    Ref<IUIAutomationTextPattern> pattern;
    Ref<IUIAutomationTextRange> range;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(pattern.put()))) || !pattern
        || FAILED(pattern->get_DocumentRange(range.put())) || !range) return false;
    VARIANT ink, paper;
    VariantInit(&ink); VariantInit(&paper);
    const bool known = SUCCEEDED(range->GetAttributeValue(UIA_ForegroundColorAttributeId, &ink)) && ink.vt == VT_I4
        && SUCCEEDED(range->GetAttributeValue(UIA_BackgroundColorAttributeId, &paper)) && paper.vt == VT_I4;
    bool faint = false;
    if (known) {
        const double a = Luminance(ink.lVal), b = Luminance(paper.lVal);
        faint = ((std::max)(a, b) + .05) / ((std::min)(a, b) + .05) < 3.0;
    }
    VariantClear(&ink); VariantClear(&paper);
    return faint;
}
KeyboardFieldState Read(IUIAutomationElement* element) {
    Ref<IUIAutomationValuePattern> value;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(value.put()))) && value) {
        Text text;
        if (SUCCEEDED(value->get_CurrentValue(&text.value)))
            return !SysStringLen(text.value) || Placeholder(element) ? KeyboardFieldState::Empty : KeyboardFieldState::Text;
    }
    Ref<IUIAutomationTextPattern> pattern;
    Ref<IUIAutomationTextRange> range;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(pattern.put()))) && pattern
        && SUCCEEDED(pattern->get_DocumentRange(range.put())) && range) {
        Text text;
        if (SUCCEEDED(range->GetText(1, &text.value)))
            return !SysStringLen(text.value) || Placeholder(element) ? KeyboardFieldState::Empty : KeyboardFieldState::Text;
    }
    return KeyboardFieldState::Unknown;
}

void Watch(std::shared_ptr<KeyboardFieldWatcher::Shared> shared) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    {
        Ref<IUIAutomation> automation;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(automation.put()))) || !automation) { CoUninitialize(); return; }
        auto* handler = new Handler(shared);
        automation->AddFocusChangedEventHandler(nullptr, handler);
        Ref<IUIAutomationElement> current;
        const PROPERTYID valueProperty = UIA_ValueValuePropertyId;
        const auto stopWatching = [&] {
            if (!current) return;
            automation->RemovePropertyChangedEventHandler(current.value, handler);
            automation->RemoveAutomationEventHandler(UIA_Text_TextChangedEventId, current.value, handler);
            current.put();
        };
        {
            // Start from whatever already has focus.
            IUIAutomationElement* focused = nullptr;
            if (SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
                handler->HandleFocusChangedEvent(focused);
                focused->Release();
            }
        }
        int ignoredPid = 0;
        bool ignoredCached = false;
        for (;;) {
            WaitForSingleObject(shared->wake, 400);
            IUIAutomationElement* focused = nullptr;
            {
                std::lock_guard<std::mutex> guard(shared->lock);
                if (!shared->alive) break;
                if (shared->focusChanged) { focused = shared->focus; shared->focus = nullptr; shared->focusChanged = false; }
            }
            if (!focused) {
                // Some apps (Chromium) send no event when focus returns to a
                // field; compare with the real focus on every check.
                IUIAutomationElement* now = nullptr;
                if (SUCCEEDED(automation->GetFocusedElement(&now)) && now) {
                    BOOL same = FALSE;
                    if (current) automation->CompareElements(current.value, now, &same);
                    if (same) now->Release(); else focused = now;
                }
            }
            if (focused && Ignored(focused, ignoredPid, ignoredCached)) { focused->Release(); focused = nullptr; }
            if (focused) {
                stopWatching();
                if (Readable(focused)) {
                    current.value = focused;
                    automation->AddPropertyChangedEventHandlerNativeArray(current.value, TreeScope_Element, nullptr, handler, const_cast<PROPERTYID*>(&valueProperty), 1);
                    automation->AddAutomationEventHandler(UIA_Text_TextChangedEventId, current.value, TreeScope_Element, nullptr, handler);
                } else focused->Release();
            }
            const auto next = current ? Read(current.value) : KeyboardFieldState::Unknown;
            if (shared->state.exchange(static_cast<LONG>(next)) != static_cast<LONG>(next)) shared->Post();
        }
        stopWatching();
        automation->RemoveAllEventHandlers();
        handler->Release();
    }
    CoUninitialize();
}
}

KeyboardFieldWatcher::KeyboardFieldWatcher(HWND notify) : shared_(std::make_shared<Shared>()) {
    shared_->notify = notify;
    if (shared_->wake) worker_ = std::thread(Watch, shared_);
}
KeyboardFieldWatcher::~KeyboardFieldWatcher() {
    {
        std::lock_guard<std::mutex> guard(shared_->lock);
        shared_->alive = false;
    }
    if (shared_->wake) SetEvent(shared_->wake);
    // Never wait for the worker here. Its current UI Automation call may be
    // waiting on this very thread (focus in jellyflower's own tray menu is
    // served by the controller's thread), and joining would hang both. The
    // worker keeps its own reference to the shared state, sees alive == false
    // and finishes by itself; it never posts once alive is cleared.
    if (worker_.joinable()) worker_.detach();
}
KeyboardFieldState KeyboardFieldWatcher::State() const noexcept {
    return static_cast<KeyboardFieldState>(shared_->state.load());
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
#include <thread>

namespace acrylic {
// Posted to the controller window when the focused field's state changes.
inline constexpr UINT KeyboardFieldMessage = WM_APP + 54;
// Values match KeyboardField in keyboard_view.hpp and StartState::keyboardField.
enum class KeyboardFieldState : LONG { Unknown = 0, Empty = 1, Text = 2 };

// Watches the focused text field through UI Automation, so the touch keyboard
// can dim its return key for an empty field and accent it once there is text.
// Only single-line or multi-line edit controls that expose their text count;
// password boxes, documents and apps that report nothing stay Unknown.
// Runs on its own MTA thread; UI Automation handlers only record and wake it.
// Every 400 ms it also re-reads the field and compares the real focus, in
// case an app sends no events. Focus inside the touch keyboard's own host or
// jellyflower is ignored, so tapping keys never loses the field. Chromium's
// placeholder text (reported as the value, drawn faint) counts as empty.
// Destroying the watcher signals the thread and returns without waiting.
class KeyboardFieldWatcher {
public:
    explicit KeyboardFieldWatcher(HWND notify);
    ~KeyboardFieldWatcher();
    KeyboardFieldWatcher(const KeyboardFieldWatcher&) = delete;
    KeyboardFieldWatcher& operator=(const KeyboardFieldWatcher&) = delete;
    KeyboardFieldState State() const noexcept;
    struct Shared;
private:
    std::shared_ptr<Shared> shared_;
    std::thread worker_;
};
}

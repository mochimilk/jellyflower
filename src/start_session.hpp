// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_shared.hpp"
#include <atomic>
#include <thread>
namespace acrylic {
// Keyboard is the touch keyboard's TextInputHost.exe, attached only while the
// symmetric layout is on (and kept attached afterwards to restore it).
enum class ShellHost { Start,Search,Keyboard };
class StartSession {
    ShellHost host_;
    Handle mapping_,process_;
    StartState* state_=nullptr;
    HANDLE remoteMapping_=nullptr;
    DWORD pid_=0;
    std::thread worker_;
    std::atomic_bool finished_{true};
    ULONGLONG lastAttempt_=0;
    void Release();
public:
    explicit StartSession(ShellHost host=ShellHost::Start):host_(host){}
    ~StartSession();
    void Refresh(bool light,HWND notify=nullptr,bool keyboard=false);
    LONG Count() const {return state_ ? Read(state_->count):0;}
    LONG KeyboardCount() const {return state_ ? Read(state_->keyboards):0;}
    // Publishes the focused field's state to the keyboard host at once.
    void KeyboardField(LONG field);
    LONG SearchCount() const {return state_ ? Read(state_->searches):0;}
    LONG SpacingCount() const {return state_ ? Read(state_->spacing):0;}
    LONG Attached() const {return state_ ? Read(state_->attached):0;}
    LONG Result() const {return state_ ? Read(state_->result):0;}
    DWORD Pid() const {return pid_;}
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
namespace acrylic {
class SetupLock {
    Handle mutex_;
    bool owned_ = false;
public:
    DWORD Acquire(const wchar_t* name = L"Local\\jellyflower.Setup.v1", DWORD timeout = 0) {
        mutex_.reset(CreateMutexW(nullptr, FALSE, name));
        if (!mutex_) return GetLastError();
        const auto wait = WaitForSingleObject(mutex_.get(), timeout);
        owned_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        return owned_ ? ERROR_SUCCESS : wait == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError();
    }
    void Release() noexcept { if (owned_) { ReleaseMutex(mutex_.get()); owned_ = false; } }
    ~SetupLock() { Release(); }
};
}

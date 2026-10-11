// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/setup_lock.hpp"
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace acrylic;
void Check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
int main() {
    try {
        const auto name = L"Local\\jellyflower.Setup.Test." + std::to_wstring(GetCurrentProcessId());
        Handle legacy(CreateMutexW(nullptr, FALSE, name.c_str()));
        Check(!!legacy, "legacy setup dialog handle");
        SetupLock first; Check(first.Acquire(name.c_str()) == 0, "an existing name is not a busy operation");
        DWORD blocked = 0;
        std::thread other([&] { SetupLock second; blocked = second.Acquire(name.c_str()); }); other.join();
        Check(blocked == ERROR_BUSY, "another actively installing thread is blocked");
        first.Release();
        std::thread after([&] { SetupLock third; blocked = third.Acquire(name.c_str()); }); after.join();
        Check(blocked == 0, "a finished install does not block updates even while its dialog handle lives");
        std::thread abandoned([&] { HANDLE handle = CreateMutexW(nullptr, TRUE, (name+L".abandoned").c_str()); (void)handle; }); abandoned.join();
        SetupLock recovered; Check(recovered.Acquire((name+L".abandoned").c_str()) == 0, "abandoned installer lock recovers");
        std::cout << "PASS: reproduced old false busy result; active install exclusion, release before dialog, abandoned lock recovery.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

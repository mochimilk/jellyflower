// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include <memory>

namespace acrylic {
// The taskbar's fluted glass, drawn in the controller's own windows (glass_state.hpp says why): one per
// slot the Explorer helper publishes, kept directly below its taskbar window over the background's
// place, which carries it into and out of the topmost band with the taskbar. It follows the taskbar
// window's moves, never takes input, and goes with the controller.
class TaskbarGlass {
public:
    TaskbarGlass();
    ~TaskbarGlass();
    TaskbarGlass(const TaskbarGlass&) = delete;
    TaskbarGlass& operator=(const TaskbarGlass&) = delete;
    // Reads the helper's slots and shows, moves or removes the windows, then publishes which slots have
    // glass in glassShown. True when that changed, so the caller tells the helper. Never throws.
    bool Refresh(SharedState* state, DWORD explorerPid, bool light) noexcept;
    // Every window gone and glassShown cleared.
    void Remove(SharedState* state) noexcept;
    HRESULT Error() const noexcept;
    static constexpr const wchar_t* WindowClass = L"jellyflower.FlutedGlass";
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

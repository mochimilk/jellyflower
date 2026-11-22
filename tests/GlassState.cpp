// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/glass_state.hpp"
#include <cstdio>
#include <stdexcept>
using acrylic::GlassSlot;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Slots() {
    acrylic::SharedState state{};
    std::array<GlassSlot, acrylic::GlassSlotCount> slots{};
    Check(acrylic::ReadGlassSlots(state, slots) && slots[0].Empty() && slots[3].Empty(), "no slots, no glass");
    const GlassSlot pill{reinterpret_cast<HWND>(static_cast<LONG_PTR>(0x210a5a)), 506, 9, 867, 174, 72};
    acrylic::WriteGlassSlot(state, 1, pill);
    Check(acrylic::Read(state.glassSequence) == 2, "a write leaves an even count");
    Check(acrylic::ReadGlassSlots(state, slots) && slots[1] == pill && slots[0].Empty() && !slots[1].Empty(), "a slot reads back whole");
    acrylic::WriteGlassSlot(state, 1, {});
    Check(acrylic::ReadGlassSlots(state, slots) && slots[1].Empty(), "a cleared slot is empty");
    acrylic::WriteGlassSlot(state, acrylic::GlassSlotCount, pill);
    Check(acrylic::Read(state.glassSequence) == 4, "a slot past the end is ignored");
    InterlockedIncrement(&state.glassSequence);
    Check(!acrylic::ReadGlassSlots(state, slots), "a slot being written is not read");
    InterlockedIncrement(&state.glassSequence);
    Check(GlassSlot{pill.taskbar, 0, 0, 0, 10, 0}.Empty() && GlassSlot{nullptr, 0, 0, 10, 10, 0}.Empty(), "no size or no taskbar, no glass");
}
void Windows() {
    const HWND high = reinterpret_cast<HWND>(static_cast<LONG_PTR>(-0x7ffe)); // Window handles are sign-extended 32-bit values.
    Check(acrylic::UnpackWindow(acrylic::PackWindow(high)) == high, "a sign-extended window handle crosses intact");
    const HWND low = reinterpret_cast<HWND>(static_cast<LONG_PTR>(0x7fff1234));
    Check(acrylic::UnpackWindow(acrylic::PackWindow(low)) == low, "a positive window handle crosses intact");
}
void Shown() {
    acrylic::SharedState state{};
    InterlockedExchange(&state.glassShown, 0b0101);
    Check(acrylic::GlassShown(state, 0) && !acrylic::GlassShown(state, 1) && acrylic::GlassShown(state, 2), "each slot has its own bit");
    Check(!acrylic::GlassShown(state, acrylic::GlassSlotCount), "past the end is never shown");
}
int main() {
    try {
        Slots();
        Windows();
        Shown();
        std::puts("PASS: fluted glass slots between the Explorer helper and the controller: whole reads under an even count, cleared and out-of-range slots, window handles across processes, shown bits.");
        return 0;
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}

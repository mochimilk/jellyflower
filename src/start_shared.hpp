// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include "search_text.hpp"
namespace acrylic {
inline constexpr GUID StartTapClass={0x64d206c7,0x55a4,0x4b53,{0x8a,0xcc,0x39,0x61,0x57,0xb1,0x90,0x68}};
inline constexpr LONG StartMagic=0x4a465315;
inline constexpr wchar_t StartNotifyClass[]=L"jellyflower.StartNotify.v21";
inline constexpr UINT StartRefreshMessage=WM_APP+73;
struct StartState {
    volatile LONG magic,controllerPid,epoch,running,enabled,light,attached,count,result,searches,spacing;
    volatile LONG gridPasses,pinnedPasses,recommendedPasses;
    // Search runs in an AppContainer. Transfer only this mapping and a
    // SYNCHRONIZE-only controller handle; never widen the named object's ACL.
    alignas(8) volatile LONG64 ownerHandle;
    volatile LONG mappingClaimed;
    // Immutable strings plus an atomic selector avoid torn UTF-16 reads in
    // Start/Search UI threads when the local time crosses a greeting boundary.
    volatile LONG greetingPeriod;
    wchar_t greetings[3*static_cast<unsigned>(Language::Count)][SearchPlaceholderCapacity];
    // Touch keyboard host (v21+): the symmetric layout switch, and how many
    // keyboard grids currently show it.
    volatile LONG keyboard,keyboards;
    // The focused text field (KeyboardField: 0 unknown, 1 empty, 2 text),
    // from the controller's UI Automation watcher, for the return key.
    volatile LONG keyboardField;
};
inline std::wstring StartMappingName(DWORD pid) { return L"Local\\jellyflower.Start.v21."+std::to_wstring(pid); }
}

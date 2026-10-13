// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include "search_text.hpp"
namespace acrylic {
inline constexpr GUID StartTapClass={0x947e31b2,0x47ac,0x4a86,{0x9b,0x6e,0x67,0xcb,0x86,0xa5,0x68,0xb1}};
inline constexpr LONG StartMagic=0x4a465312;
inline constexpr wchar_t StartNotifyClass[]=L"jellyflower.StartNotify.v18";
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
    wchar_t greetings[3][SearchPlaceholderCapacity];
};
inline std::wstring StartMappingName(DWORD pid) { return L"Local\\jellyflower.Start.v18."+std::to_wstring(pid); }
}

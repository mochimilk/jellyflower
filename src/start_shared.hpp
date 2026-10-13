// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include "search_text.hpp"
namespace acrylic {
inline constexpr GUID StartTapClass={0x97c45b36,0x47ac,0x4a86,{0x9b,0x6e,0x67,0xcb,0x86,0xa5,0x68,0xb1}};
inline constexpr LONG StartMagic=0x4a46530f;
inline constexpr wchar_t StartNotifyClass[]=L"jellyflower.StartNotify.v15";
inline constexpr UINT StartRefreshMessage=WM_APP+73;
struct StartState {
    volatile LONG magic,controllerPid,epoch,running,enabled,light,attached,count,result,searches,spacing;
    volatile LONG gridPasses,pinnedPasses,recommendedPasses;
    // Search runs in an AppContainer. Transfer only this mapping and a
    // SYNCHRONIZE-only controller handle; never widen the named object's ACL.
    alignas(8) volatile LONG64 ownerHandle;
    volatile LONG mappingClaimed;
    wchar_t placeholder[SearchPlaceholderCapacity];
};
inline std::wstring StartMappingName(DWORD pid) { return L"Local\\jellyflower.Start.v15."+std::to_wstring(pid); }
}

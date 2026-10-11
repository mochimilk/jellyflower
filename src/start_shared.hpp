// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
namespace acrylic {
inline constexpr GUID StartTapClass={0xfaf0a3d1,0x47ac,0x4a86,{0x9b,0x6e,0x67,0xcb,0x86,0xa5,0x68,0xb1}};
inline constexpr LONG StartMagic=0x4a465303;
inline constexpr wchar_t StartNotifyClass[]=L"jellyflower.StartNotify.v3";
inline constexpr UINT StartRefreshMessage=WM_APP+73;
struct StartState {
    volatile LONG magic,controllerPid,epoch,running,enabled,light,attached,count,result,searches;
};
inline std::wstring StartMappingName(DWORD pid) { return L"Local\\jellyflower.Start.v3."+std::to_wstring(pid); }
}

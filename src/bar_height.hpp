// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace acrylic {
// A taller taskbar for the floating dock. Explorer sizes the bar's window
// from TrayUI::GetMinSize(HMONITOR, SIZE*), a virtual method in Taskbar.dll.
// Its vtable slot is pointed at a function that calls the stock method and
// then sets the height; no code is rewritten. The dock holds the XAML frames
// at the same height (dock.hpp).
//
// module: Taskbar.dll's base. slot, function: RVAs of the vtable slot and of
// the stock method it holds, read by the controller from Microsoft's public
// symbols. height: DIPs, or 0 for the stock bar. The slot is written only
// while it holds the stock method or ours; anything else is
// ERROR_REVISION_MISMATCH and changes nothing.
HRESULT SetBarHeight(BYTE* module, DWORD slot, DWORD function, LONG height) noexcept;
// The height being answered in DIPs, 0 for the stock bar.
LONG BarHeight() noexcept;
// Makes Explorer measure the taskbar again, as a display-scale change does.
// Call on the taskbar's thread.
void RemeasureTaskbar() noexcept;
}

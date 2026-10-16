// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.h>
namespace acrylic {
void TrackAdaptive(unsigned long long handle,winrt::Windows::UI::Xaml::DependencyObject const& object);
void RemoveAdaptive(unsigned long long handle);
void RefreshAdaptive(HINSTANCE module,SharedState* state,bool(*alive)()) noexcept;
}

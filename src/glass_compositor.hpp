// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Composition.h>

namespace acrylic {
// The controller thread's compositor for fluted glass and the reeds' effect, with the dispatcher queue
// composition needs. Made on first use and kept for the process, because making them for every menu
// would delay it opening; deliberately leaked, so nothing is released after the apartment shuts down.
// Both throw if composition isn't available.
winrt::Windows::UI::Composition::Compositor GlassCompositor();
winrt::Windows::UI::Composition::CompositionEffectFactory GlassReeds();
}

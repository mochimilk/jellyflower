// SPDX-License-Identifier: GPL-3.0-or-later
#include "glass_compositor.hpp"
#include "fluted_glass.hpp"
#include <DispatcherQueue.h>
#include <winrt/Windows.System.h>

namespace acrylic {
namespace {
namespace composition = winrt::Windows::UI::Composition;
struct Glass {
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    composition::Compositor compositor{nullptr};
    composition::CompositionEffectFactory reeds{nullptr};
};
Glass& Ready() {
    static auto* glass = new Glass;
    if (!glass->queue && !winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
        const DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(glass->queue))));
    }
    if (!glass->compositor) glass->compositor = composition::Compositor();
    if (!glass->reeds) glass->reeds = CreateFlutedEffect(glass->compositor);
    return *glass;
}
}
composition::Compositor GlassCompositor() { return Ready().compositor; }
composition::CompositionEffectFactory GlassReeds() { return Ready().reeds; }
}

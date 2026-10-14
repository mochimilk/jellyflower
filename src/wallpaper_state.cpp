// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpaper.hpp"
#include <algorithm>

namespace acrylic {
namespace {
bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
const WallpaperMonitor* Find(const WallpaperDesktop& desktop, const std::wstring& id) {
    for (const auto& monitor : desktop.monitors) if (Same(monitor.id, id)) return &monitor;
    return nullptr;
}
}
HRESULT WallpaperEngine::Update(bool enabled, bool light) {
    if (!state.enabled && !enabled) return S_OK;
    std::wstring lightImage, darkImage;
    HRESULT result = backend.Assets(lightImage, darkImage);
    if (FAILED(result)) return result;
    WallpaperDesktop current;
    result = backend.Capture(current, false);
    if (FAILED(result)) return result;
    auto owned = [&](const WallpaperMonitor& monitor) { return Same(monitor.image, lightImage) || Same(monitor.image, darkImage); };
    bool external = false, canRestorePosition = true;
    std::vector<std::wstring> restore;
    if (state.enabled) {
        for (const auto& monitor : current.monitors) {
            const auto* prior = Find(state.previous, monitor.id);
            if(!monitor.attached){if(prior && owned(monitor))restore.push_back(monitor.id);continue;}
            if (owned(monitor)) { if (prior) restore.push_back(monitor.id); }
            else if (prior) {
                // A completed partial rollback can already have restored this monitor.
                const bool original = Same(monitor.image, prior->image) || (!prior->backup.empty() && Same(monitor.image, prior->backup));
                external = true;
                canRestorePosition = canRestorePosition && original;
            } else canRestorePosition = false;
        }
    }
    if (state.enabled && (!enabled || external)) {
        // Respect an outside wallpaper choice. Restore only monitors still using
        // our images; never reapply over the user's new choice on the next theme.
        result = backend.Restore(state.previous, restore, canRestorePosition && current.position == 4 /* DWPOS_FILL */);
        if (FAILED(result)) return result;
        WallpaperState off;
        result = backend.Save(off);
        if (SUCCEEDED(result)) state = std::move(off);
        return result;
    }
    const bool first = !state.enabled;
    if (first) {
        WallpaperState next;
        next.enabled = true;
        result = backend.Capture(next.previous, true);
        if (FAILED(result)) return result;
        // Commit the recovery information before touching Windows. It survives
        // an interrupted update, process crash, restart or version replacement.
        result = backend.Save(next);
        if (FAILED(result)) return result;
        state = std::move(next);
    } else {
        // Capture newly connected monitors before including them in the apply list.
        WallpaperState next = state;
        bool added = false;
        for (const auto& monitor : current.monitors) if (monitor.attached && !Find(state.previous, monitor.id)) {
            WallpaperDesktop backup;
            result = backend.Capture(backup, true);
            if (FAILED(result)) return result;
            for (const auto& extra : backup.monitors) if (!Find(next.previous, extra.id)) {
                next.previous.monitors.push_back(extra); added = true;
            }
            break;
        }
        if (added) { result = backend.Save(next); if (FAILED(result)) return result; state = std::move(next); }
        const auto& target = light ? lightImage : darkImage;
        if (current.position == 4 && !current.monitors.empty() && std::all_of(current.monitors.begin(), current.monitors.end(),
            [&](const WallpaperMonitor& monitor) { return !monitor.attached || Same(monitor.image, target); })) return S_OK;
    }
    std::vector<std::wstring> targets;
    for(const auto& monitor:current.monitors)if(monitor.attached && Find(state.previous,monitor.id))targets.push_back(monitor.id);
    result = backend.Apply(light ? lightImage : darkImage,targets);
    if (FAILED(result) && first) {
        // Apply may have changed the position or only some screens before failing.
        // Roll back the captured desktop, keeping its recovery record on failure.
        std::vector<std::wstring> ids;
        for (const auto& monitor : state.previous.monitors) ids.push_back(monitor.id);
        if (SUCCEEDED(backend.Restore(state.previous, ids, true))) {
            WallpaperState off;
            if (SUCCEEDED(backend.Save(off))) state = std::move(off);
        }
    }
    return result;
}
} // namespace acrylic

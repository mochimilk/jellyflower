// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
namespace acrylic {
inline constexpr UINT AutoThemeChangedMessage = WM_APP + 91;
enum class AutoThemeStatus : UINT { Manual, Automatic, Locating, Denied, Unavailable, Error };
class AutoTheme {
public:
    explicit AutoTheme(HWND window);
    ~AutoTheme();
    void Enable(); // Call on the foreground UI thread, only after a menu click.
    DWORD Disable();
    // Call only for its timer, async location/permission events, or system changes.
    // Returns the next one-shot delay (zero means no automatic work is pending).
    bool Update(bool& light, UINT& delay, bool invalidate = false);
    AutoThemeStatus Status() const;
    DWORD Error() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

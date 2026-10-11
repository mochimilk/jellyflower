// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
namespace acrylic {
enum class AutoThemeStatus : UINT { Manual, Automatic, Locating, Denied, Unavailable, Error };
class AutoTheme {
public:
    AutoTheme();
    ~AutoTheme();
    void Enable(); // Call on the foreground UI thread, only after a menu click.
    DWORD Disable();
    bool Tick(bool& light); // Applies no settings itself. Poll on the UI thread.
    AutoThemeStatus Status() const;
    DWORD Error() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

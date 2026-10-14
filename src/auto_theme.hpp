// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <memory>
namespace acrylic {
inline constexpr UINT AutoThemeChangedMessage = WM_APP + 91;
inline constexpr UINT WallpaperAutoThemeChangedMessage = WM_APP + 92;
enum class AutoThemeScope { System, Wallpaper };
inline const wchar_t* AutoThemeValue(AutoThemeScope scope){return scope==AutoThemeScope::System?L"AutoTheme":L"WallpaperAutoTheme";}
inline const wchar_t* AutoLocationValue(AutoThemeScope scope){return scope==AutoThemeScope::System?L"ThemeLocation":L"WallpaperThemeLocation";}
enum class AutoThemeStatus : UINT { Manual, Automatic, Locating, Denied, Unavailable, Error };
class AutoTheme {
public:
    explicit AutoTheme(HWND window,AutoThemeScope scope=AutoThemeScope::System);
    ~AutoTheme();
    void Enable(); // Call on the foreground UI thread, only after a menu click.
    DWORD Disable();
    // Call only for its timer, one-shot completion, or system changes.
    // Returns the next one-shot delay (zero means no automatic work is pending).
    bool Update(bool& light, UINT& delay, bool invalidate = false);
    AutoThemeStatus Status() const;
    DWORD Error() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

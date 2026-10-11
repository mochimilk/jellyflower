# jellyflower

A small native Windows utility for an acrylic taskbar, a single-line clock, and cursor colors. Open the flower under **show hidden icons** to use its lowercase acrylic menu. Click the flower again or click outside to close it. Setting changes keep the menu open.

## Download and install

Download [jellyflower-setup-x64.exe](https://github.com/mochimilk/jellyflower/releases/latest/download/jellyflower-setup-x64.exe) from [GitHub Releases](https://github.com/mochimilk/jellyflower/releases), then double-click it. No compiler, PowerShell commands, administrator rights, or runtime download is required.

Setup installs into `%LOCALAPPDATA%\Programs\jellyflower`, adds a Start menu shortcut, enables login startup for your Windows account, and launches the app. Quit TranslucentTB first. Windows Settings > Apps > Installed apps > jellyflower removes startup and restores the taskbar. Exit in the tray menu stops it for this session and restores the clock, speaker, taskbar material, and configured Windows cursor scheme.

The installer and EXE are currently unsigned. The first downloadable build is x64; the source and release workflow also support native ARM64. Use an installer matching Windows Explorer's architecture. Windows 11's modern stock taskbar is the primary target; the legacy acrylic backend also supports Windows 10 build 17134+ and early Windows 11. Single-line clock, speaker, chevron, and border adapters require supported Windows 11 XAML layouts.

## Menu

| Row | Behavior |
| --- | --- |
| system | Cycles `light`, `dark`, `auto` at the right. Changes Windows system and app mode; auto uses your approved location for sunrise/sunset. |
| acrylic | Cycles the right-side status through `system`, `pure`, and `off`. Startup uses `system`: white tint in light mode, neutral dark tint in dark mode. Pure has no tint. |
| date | Toggles the date in the single-line clock, with `on` or `off` at the right and the matching calendar icon. |
| colors / cursor | Before import: `colors` with `unlock` at the right opens a text guide and your import folder; its preview cycles through colors. After import: `cursor` opens the color choices, default (system), and disable. There is no redundant unlock row. |
| v0.15.0 | Shows your installed version. Select it to check GitHub. When it says `available`, select it again to download, verify, install, and relaunch inside jellyflower. |
| exit | Stops jellyflower and restores the shell appearance. Login startup remains enabled. |

The clock reads `Mon Jun 22  9:41 am`, with lowercase am/pm, retaining Windows' calendar button and tooltip. The clock follows the native Windows system font. Custom fonts and the clock/font submenus have been removed; old font selections are ignored. On supported Windows 11 layouts the speaker glyph is hidden while network, battery, and Quick Settings remain; Win+A still provides the volume slider. The show-hidden-icons chevron uses 60% opacity. An active acrylic mode hides the taskbar's top border.

The tray uses the supplied filled flower silhouette with a transparent center and background: black in light mode, white in dark mode. The application and installed Start shortcut keep the rounded flower tile with inverse light/dark variants. Windows caches shortcut icons, so their visible refresh can lag. The icon embedded in an EXE's file metadata is static; Explorer uses the supplied default artwork there.

Menus use native accent acrylic with an explicit translucent theme tint; Windows' default transient material was almost opaque on some builds. One foreground surface preserves text/icon alpha. Labels remain 10pt regular Segoe UI with consistent grayscale coverage in both themes. High contrast, disabled transparency, unsupported systems, and compositor/power policy can produce a solid fallback. This accent API is undocumented and may change with Windows.

## Automatic system theme

Choose `auto` on the first `system` row. Windows asks for location permission when needed; jellyflower requests city-level accuracy, rounds coordinates to 0.1 degrees, saves them only in your account's local settings, and calculates daylight locally using the [NOAA solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF). Jellyflower does not upload coordinates or send them to GitHub or a sunrise API. Windows location services may use Microsoft's network services. Location is refreshed about once a day; changes after travel may take that long to appear. The schedule runs only while jellyflower is running and catches up after sleep. Solar times are approximate, typically within a few minutes; polar day/night follow the sun's elevation.

Until a location is available, the current theme stays unchanged. Denied/disabled location appears in the row; enable Location in Windows privacy settings and select auto again to retry. A background login launch never explicitly requests new consent. Choosing light/dark turns off scheduling and clears jellyflower's saved coordinates. Exit stops scheduling and leaves the last system theme selected. High contrast is not changed. Other automatic-theme tools can compete, so use one scheduler at a time.

## Optional cursor pack

The cursor pack is licensed separately and is **not bundled**. A fresh install keeps your configured Windows cursor and shows a single `colors / unlock` action. After a complete import, the cursor submenu includes all colors and disable.

Choose **colors / unlock** to open a text guide and `%LOCALAPPDATA%\jellyflower\cursors` in Explorer. Get your own full ZIP from [JepriCreations](https://jepricreations.com/products/cursor-concept-3-hdpi), then drop the original ZIP into that folder. Any ZIP filename is fine. Keep jellyflower running; it checks within ten seconds. Do not extract it or run its CMD/INF installers. The free 32px light/dark ZIP is a different pack and does not unlock the full color set.

Importing enables the choices without switching your cursor. Select a color or **default (system)** afterward. Jellyflower validates all 153 files and imports only the 17 cursor roles for each of the nine colors. Failed or incomplete imports leave the active pack and cursor alone. Your ZIP and imported files stay in your user folder, survive updates/uninstall, and are never uploaded. Removing them is a separate manual action.

The 17 standard cursor roles, including animated busy cursors, are applied temporarily without overwriting your saved Windows cursor scheme. The menu SVG uses the selected cursor's actual body and outline colors. Earlier explicitly configured pack locations remain supported; advanced users can configure a complete extracted pack with:

```powershell
.\jellyflower.exe --cursor-pack "C:\path\to\cursor-concept-3-hdpi"
```

Cursor and date preferences retain the internal `HKCU\Software\AcrylicTaskbar` key for compatibility with earlier builds.

No cursor pack files are distributed in the app or source archive, or installed into Windows directories. Only a user's own ZIP is extracted into their private jellyflower data folder. Apps drawing their own cursors may ignore system cursor changes. If jellyflower is force-ended, reload the saved Windows scheme with `jellyflower.exe --restore-cursors` after stopping it, or sign out.

## Updates

Releases are hosted at [mochimilk/jellyflower](https://github.com/mochimilk/jellyflower/releases). Checks happen only when requested and run in the background. The menu reports results without system notifications. An available update uses an open package; selecting it downloads and verifies the native installer, then quietly replaces and relaunches the app. The spinner stays animated during checking/downloading/installing. No browser, administrator rights, account, or token is needed. Saved preferences and your cursor pack survive. A failed download leaves the running app alone; setup retains the previous files and attempts to restore registration/relaunch on an installation failure. See [UPDATES.md](docs/UPDATES.md).

Each setup stages a separate version folder before stopping the old controller. Windows pins the Explorer helper until Explorer exits; keep old version folders until then. Controller/menu-only changes take effect immediately. Upgrading from a pre-0.14.1 loaded helper requires one sign-out/sign-in to load the v9 taskbar tint. This release retains that helper protocol. A normal installer displays the reminder when needed; in-app updates never restart Explorer. The replacement briefly restores the shell while the old controller exits. Setup releases its installation lock before displaying completion, fixing false Windows error 170 on subsequent updates.

## Battery saver and compatibility

jellyflower stays running and keeps your selected acrylic mode when battery saver is enabled. Windows' compositor can still replace the system acrylic brush with a solid fallback under battery saver, accessibility, transparency, or rendering policies. Process liveness and accepted brush requests cannot prove visible blur. See [INVESTIGATION.md](docs/INVESTIGATION.md) and [TESTING.md](docs/TESTING.md).

The modern backend is a small Explorer diagnostic/TAP helper using Windows' system AcrylicBrush. It tracks replacement taskbar elements, retains its connection across transient XAML island changes, and restores saved properties/bindings on graceful exit or controller loss. It relies on internal shell element names; Windows updates and competing shell utilities can affect it. Unknown clock/speaker structures are left alone. There is no compositor bypass, elevation, or memory patching.

When switching from TranslucentTB, close it and restart Explorer once if the helper remains at bridgeState 1 with no accepted backgrounds. On the development Windows build 26200, that resolved the initial attachment stall. Desktop click, tooltip, cursor replacement, tint, and battery-saver appearance remain visual checks; [VALIDATION.md](docs/VALIDATION.md) distinguishes build/test results from desktop confirmation.

## Build from source

Install Visual Studio 2022 Build Tools with Desktop development with C++, C++ CMake tools, and Windows SDK 10.0.19041 or newer. Use CMake 3.24+. Then run:

```powershell
.\Build.ps1
```

Double-click `dist\x64\jellyflower-setup-x64.exe` to install. To build ARM64, install its C++ tools and run `Build.ps1 -Architecture ARM64`. Checked-in ICO files and native cursor geometry mean Python and SVG libraries are optional, only needed for regenerating artwork. The C++ runtime is linked statically; Windows supplies the remaining libraries.

To run a portable build, keep `jellyflower.exe`, `jellyflower-bridge.dll`, both app ICOs, and license/notices together. `Install.ps1` in the distribution enables startup from that exact directory. `Diagnose.ps1` writes the controller's JSON status. Maintenance commands include `--stop`, `--install`, `--uninstall`, `--diagnose <file>`, `--verify-cursors <folder>`, and `--restore-cursors`. A five-second recovery timer complements system and shell events. Logs are bounded to roughly 64 KiB in `%LOCALAPPDATA%\jellyflower\diagnostics.log`.

The GitHub workflow builds x64 and ARM64 and publishes tagged releases. `Publish.ps1` publishes the first local x64 build using the maintainer's normal GitHub CLI login. [UPDATES.md](docs/UPDATES.md) documents the release process.

## License

GPL-3.0-or-later. See [LICENSE.md](LICENSE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an independent project using simplified techniques adapted from TranslucentTB. Corresponding source is provided with each release. External cursor licenses remain with their respective authors.

# jellyflower

A small native Windows utility for an acrylic taskbar, a single-line clock, cursor colors, and matching Start menu acrylic. Open the flower under **show hidden icons** to use its lowercase acrylic menu. Click the flower again or click outside to close it. Setting changes keep the menu open. Hovering the flower shows `ur cute :3`.

## Download and install

Download [jellyflower-setup-x64.exe](https://github.com/mochimilk/jellyflower/releases/latest/download/jellyflower-setup-x64.exe) from [GitHub Releases](https://github.com/mochimilk/jellyflower/releases), then double-click it. No compiler, PowerShell commands, administrator rights, or runtime download is required.

Setup installs into `%LOCALAPPDATA%\Programs\jellyflower`, adds a Start menu shortcut, enables login startup for your Windows account, and launches the app. Quit TranslucentTB first. Windows Settings > Apps > Installed apps > jellyflower removes startup and restores the taskbar. General > exit stops it for this session and restores Start styling, the clock, speaker, taskbar material, and configured Windows cursor scheme.

Startup uses a normal-user Task Scheduler logon task named `jellyflower-<your SID>`, with no added trigger delay, password, elevation, network/idle requirement, or battery restriction. It has no execution time limit. A matching Run entry remains as a delayed fallback and protects upgrades from obsolete uninstallers; duplicate launches immediately exit through the session mutex. To disable automatic launch manually, disable this task in Task Scheduler and the fallback in Startup apps. Uninstall removes both. Jellyflower applies the taskbar before cursor-pack validation and download housekeeping, retries shell discovery every 100 ms for up to 30 seconds, then returns to five-second maintenance. Windows can still display its normal taskbar before this user app attaches; zero visible default frames cannot be guaranteed.

The installer and EXE are currently unsigned. The first downloadable build is x64; the source and release workflow also support native ARM64. Use an installer matching Windows Explorer's architecture. Windows 11's modern stock taskbar is the primary target; the legacy acrylic backend also supports Windows 10 build 17134+ and early Windows 11. Single-line clock, speaker, chevron, and border adapters require supported Windows 11 XAML layouts.

## Menu

| Row | Behavior |
| --- | --- |
| system | Cycles `light`, `dark`, `auto` at the right. Changes Windows system and app mode; auto uses your approved location for sunrise/sunset. |
| acrylic | Cycles the right-side status through `system`, `pure`, and `off`. Startup uses `system`: white tint in light mode, neutral dark tint in dark mode. Pure has no tint. |
| date | Toggles the date in the single-line clock, with `on` or `off` at the right and the matching calendar icon. |
| colors / cursor | Before import: `colors` with `unlock` at the right opens a text guide and your import folder; its preview cycles through colors. After import: `cursor` opens the color choices, default (system), and disable. There is no redundant unlock row. |
| general | Opens jellyflower wallpaper, GitHub, the version/update row and exit, with the supplied settings gear. |

Inside **general**, **github** opens the project repository in your browser, with a secondary-colored external-link indicator on hover or keyboard focus. Nested-menu chevrons use the same secondary color; selected cursor choices use the Lucide checkmark. A divider separates exit from the update row. `v0.16.20` shows the installed version. Select it to check GitHub; when the secondary label says `download`, select it again to verify, install, and relaunch inside jellyflower. **exit** uses red text in light/dark modes, respects Windows contrast colors, restores the shell appearance and leaves login startup enabled.

Click an open parent row (general or cursor) to collapse its submenu. It stays closed while the pointer remains over that row; move away and hover back to reopen normally. Another click or keyboard navigation can also reopen it.

The clock reads `Mon Jun 22  9:41 am`, with lowercase am/pm, retaining Windows' calendar button and tooltip. The clock follows the native Windows system font. Custom fonts and the clock/font submenus have been removed; old font selections are ignored. On supported Windows 11 layouts the speaker glyph is hidden while battery and Quick Settings remain. Wi-Fi/Ethernet and offline icons use Windows' normal artwork; the former Element 2 replacement has been removed. Win+A still provides the volume slider. The show-hidden-icons chevron uses 60% opacity. An active acrylic mode hides the taskbar's top border.

The tray uses the supplied filled flower silhouette with a transparent center and background: black in light mode, white in dark mode. The application and installed Start shortcut keep the rounded flower tile with inverse light/dark variants. Windows caches shortcut icons, so their visible refresh can lag. The icon embedded in an EXE's file metadata is static; Explorer uses the supplied default artwork there.

Menus use native accent acrylic with an explicit translucent theme tint; Windows' default transient material was almost opaque on some builds. One foreground surface preserves text/icon alpha. Primary and secondary labels inherit Windows' menu font family, size and weight, with no app-defined typeface or text style. DirectWrite/Direct2D preserves those Windows metrics and draws native grayscale text on the translucent surface; the Windows GUI font is the fallback if the menu-metrics query fails. Root menus reserve 12 DIP at the work-area edges and rise 32 DIP over 250 ms with Microsoft's documented Direct Entrance cubic-bezier(0,0,0,1). Motion is skipped when Windows disables menu/client animations or in high contrast. This follows Fluent guidance; Microsoft does not publicly specify the exact taskbar flyout timing. Submenus keep their existing immediate/delayed hover behavior. High contrast, disabled transparency, unsupported systems, and compositor/power policy can produce a solid fallback. This accent API is undocumented and may change with Windows.

Light-mode system taskbar acrylic uses 0.30 white tint and 0.30 brightness-layer opacity, targeting roughly 70% transparency (30% white opacity); legacy alpha is 77/255. Windows acrylic combines blur and material layers, so this is an approximate visual target. The lower white wash can reduce dark-text contrast over very dark wallpaper; it does not guarantee 4.5:1 contrast there. Dark mode keeps tint 0.34, brightness-layer opacity 0.72 and legacy alpha 184/255. Explorer v15 is introduced in 0.16.17. Upgrading from an older loaded Explorer helper requires one sign-out/sign-in or restart; taskbar customization is paused until then. Fresh installations need no extra sign-out.

## Start menu acrylic

On supported Windows 11 Start layouts, jellyflower also applies desktop acrylic to Start's outer background and removes its opaque overlay. It shares the menu's light/dark tint colors, 184/255 tint opacity, border colors, and rounded styling. The search header is borderless with a `Search <PC name>` placeholder using the Windows device name, a 20-DIP Lucide magnifier in the primary text color, and a full-width thin divider underneath. Search retains its native font, click/keyboard behavior and animations, and stays aligned when opening Show all. Shared spacing tokens give headings/search and row content a roomier 32-DIP inset; row hover backgrounds extend 12 DIP beyond that content. Recognized category, app grid/list, pinned and Recommended views size their inner templates from the current viewport. Columns distribute evenly, lists fill the available width, and category cards and their inner two-by-two targets follow the responsive slots. Pinned apps and collapsed folders show only centered icons, keeping native tooltip/accessibility names and interaction. Pinned page-indicator space stays within the balanced insets. Folder titles and rename fields retain native behavior. The native account badge keeps its original left/right padding; the top/bottom use two DIP less than those side values. Its native content and label are vertically centered, including when a shell minimum adds extra height. Its 28-DIP avatar and 10-DIP hover radius remain; native footer hosts size to the taller content so it is not clipped. The redundant account-name tooltip is hidden, while native account actions and typography remain. Exiting restores all owned tooltip, padding and footer-height values/bindings. Outer material and shadow corners use a 20-DIP radius; the native inset background uses a matching 19-DIP radius. Windows retains ownership of its compositor clips. This runs independently of the taskbar-only acrylic selector. Exiting restores original sizing, margins, captions, header, background, overlay, border values and bindings; controller loss is checked about once a second while Start's UI thread runs. Disabled Windows transparency and high contrast restore stock styling. Unknown layouts are left unchanged.

The editable Windows Search results header also uses the shared 20-DIP magnifier, inset spacing and bottom divider, without its native pill fill/outline. Its text control, caret, selection, typography and results remain native. Search must be opened normally by the user; jellyflower does not launch it.

A separate small XAML diagnostics helper connects to StartMenuExperienceHost and SearchHost without replacing Start, requiring Windhawk, or restarting it. The connections run in the background after taskbar attachment. Windows Search is sandboxed, so jellyflower grants its verified package read/execute access to this one app-owned helper DLL. Its directory and other files keep their existing permissions. The connection transfers only the helper shared-state mapping and a controller-lifetime handle; it does not inspect the typed query. Windows updates can change private element names; unknown surfaces are left alone. DWM menu acrylic and XAML Start acrylic use different blur pipelines, so matching inputs are not a promise of identical pixels. Windows compositor/battery policy can still select a solid fallback. See [START_MENU.md](docs/START_MENU.md) for research, test steps and limitations.

## Automatic system theme

Choose `auto` on the first `system` row. Windows asks for location permission when needed; jellyflower requests city-level accuracy, rounds coordinates to 0.1 degrees, saves them only in your account's local settings, and calculates daylight locally using the [NOAA solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF). Jellyflower does not upload coordinates or send them to GitHub or a sunrise API. Windows location services may use Microsoft's network services. A day of sunrise/sunset transitions is calculated once and cached. A separate non-waking timer is armed for the next transition or daily refresh; the five-second taskbar recovery loop does not poll location permissions or recalculate solar positions. Location permission changes notify the app directly. Sleep/resume, clock changes and refreshed coordinates rebuild the schedule and catch up immediately. Location is refreshed about once a day; changes after travel may take that long to appear. The schedule runs only while jellyflower is running and catches up after sleep. Solar times are approximate, typically within a few minutes; polar day/night follow the sun's elevation.

The row immediately shows `auto` and a sunset icon, including while Windows is obtaining location. Until a location is available, the current theme stays unchanged. Denied/disabled location appears in the row; enable Location in Windows privacy settings and select auto again to retry. A background login launch never explicitly requests new consent. Choosing light/dark turns off scheduling and clears jellyflower's saved coordinates. Exit stops scheduling and leaves the last system theme selected. High contrast is not changed. Other automatic-theme tools can compete, so use one scheduler at a time.

## Optional cursor pack

The cursor pack is licensed separately and is **not bundled**. A fresh install keeps your configured Windows cursor and shows a single `colors / unlock` action. After a complete import, the cursor submenu includes all colors and disable.

Choose **colors / unlock** to open a text guide and `%LOCALAPPDATA%\jellyflower\cursors` in Explorer. Get your own full ZIP from [JepriCreations](https://jepricreations.com/products/cursor-concept-3-hdpi), then drop the original ZIP into that folder. Any ZIP filename is fine. Keep jellyflower running; it checks within ten seconds. Do not extract it or run its CMD/INF installers. The free 32px light/dark ZIP is a different pack and does not unlock the full color set.

Importing enables the choices without switching your cursor. Hover a choice to preview its actual arrow over the menu, including **default (system)** and **disable**. Each hovered arrow loads once per submenu and is reused on later visits. Hovering does not replace Windows' cursor set or save settings; moving away immediately uses your selected cursor again. Clicking applies the full cursor set and saves the choice without closing the menu; the checkmark changes only on selection. Jellyflower validates all 153 files and imports only the 17 cursor roles for each of the nine colors. Failed or incomplete imports leave the active pack and cursor alone. Your ZIP and imported files stay in your user folder, survive updates/uninstall, and are never uploaded. Removing them is a separate manual action.

The 17 standard cursor roles, including animated busy cursors, are applied temporarily without overwriting your saved Windows cursor scheme. The menu SVG uses the selected cursor's actual body and outline colors. Earlier explicitly configured pack locations remain supported; advanced users can configure a complete extracted pack with:

```powershell
.\jellyflower.exe --cursor-pack "C:\path\to\cursor-concept-3-hdpi"
```

Cursor and date preferences retain the internal `HKCU\Software\AcrylicTaskbar` key for compatibility with earlier builds.

No cursor pack files are distributed in the app or source archive, or installed into Windows directories. Only a user's own ZIP is extracted into their private jellyflower data folder. Apps drawing their own cursors may ignore system cursor changes. If jellyflower is force-ended, reload the saved Windows scheme with `jellyflower.exe --restore-cursors` after stopping it, or sign out.

## Updates

Releases are hosted at [mochimilk/jellyflower](https://github.com/mochimilk/jellyflower/releases). Checks happen only when requested and run in the background. The menu reports results without system notifications. An available update uses an open package; selecting it downloads and verifies the native installer, then quietly replaces and relaunches the app. The spinner stays animated during checking/downloading/installing. No browser, administrator rights, account, or token is needed. Saved preferences and your cursor pack survive. A failed download leaves the running app alone; setup retains the previous files and attempts to restore registration/relaunch on an installation failure. See [UPDATES.md](docs/UPDATES.md).

Each setup stages a separate version folder before stopping the old controller. Windows pins the Explorer helper until Explorer exits; keep old version folders until then. Controller/menu-only changes take effect immediately. Upgrading from an earlier loaded helper requires one sign-out/sign-in to load the current Explorer v15 helper. Start/Search continues to use its separate v16 helper. Old helpers are rejected until Explorer reloads; clock/material changes can remain unapplied until then. A normal installer displays the reminder when needed; in-app updates never restart Explorer. The replacement briefly restores the shell while the old controller exits. Setup releases its installation lock before displaying completion, fixing false Windows error 170 on subsequent updates.


## jellyflower wallpaper

Choose **general > jellyflower wallpaper** to turn the included wallpaper on or off. The supplied light/dark PNGs follow Windows' **system** color preference, including jellyflower's sunrise/sunset auto mode. This option starts off. Enabled wallpapers use Fill on every connected monitor and remain selected across app exits, updates and login. Theme changes are handled on a sleeping background worker, without a wallpaper polling timer or menu-thread file work.

Before enabling, jellyflower saves each monitor's current image, an untouched recovery copy and the image placement under `%LOCALAPPDATA%\jellyflower\wallpaper`. Turning it off restores those images/placement. Choosing a different wallpaper outside jellyflower stops theme following and preserves the new choice; other screens still showing a jellyflower image are restored. New connected monitors are backed up before applying. Errors appear as **try again** in the row, with details in diagnostics.log.

Restoration covers the currently displayed pictures and placement. Windows Spotlight or a slideshow may need to be reselected in Windows Personalization to resume their rotation; jellyflower does not save or rewrite private Windows theme/provider settings. Keep the local wallpaper data while enabled. Disable the option before uninstalling if you want the previous wallpaper restored. Windows policy can prevent desktop changes.

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

To run a portable build, keep `jellyflower.exe`, `jellyflower-bridge.dll`, `jellyflower-start-v16.dll`, both app ICOs, and license/notices together. `Install.ps1` in the distribution enables startup from that exact directory. `Diagnose.ps1` writes the controller's JSON status. Maintenance commands include `--stop`, `--install`, `--uninstall`, `--diagnose <file>`, `--verify-cursors <folder>`, and `--restore-cursors`. A five-second taskbar recovery timer complements system and shell events; automatic theme scheduling uses its separate event timer. Other shell liveness/clock timers still run, so this change alone is not a measured battery-life improvement. Logs are bounded to roughly 64 KiB in `%LOCALAPPDATA%\jellyflower\diagnostics.log`.

The GitHub workflow builds x64 and ARM64 and publishes tagged releases. `Publish.ps1` publishes the first local x64 build using the maintainer's normal GitHub CLI login. [UPDATES.md](docs/UPDATES.md) documents the release process.

## License

GPL-3.0-or-later. See [LICENSE.md](LICENSE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an independent project using simplified techniques adapted from TranslucentTB. Corresponding source is provided with each release. External cursor licenses remain with their respective authors.

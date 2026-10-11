# jellyflower

A small native Windows utility for an acrylic taskbar, a single-line clock, and cursor colors. Open the flower under **show hidden icons** to use its lowercase acrylic menu. Click the flower again or click outside to close it. Setting changes keep the menu open.

## Download and install

Download [jellyflower-setup-x64.exe](https://github.com/mochimilk/jellyflower/releases/latest/download/jellyflower-setup-x64.exe) from [GitHub Releases](https://github.com/mochimilk/jellyflower/releases), then double-click it. No compiler, PowerShell commands, administrator rights, or runtime download is required.

Setup installs into `%LOCALAPPDATA%\Programs\jellyflower`, adds a Start menu shortcut, enables login startup for your Windows account, and launches the app. Quit TranslucentTB first. Windows Settings > Apps > Installed apps > jellyflower removes startup and restores the taskbar. Exit in the tray menu stops it for this session and restores the clock, speaker, taskbar material, and configured Windows cursor scheme.

The installer and EXE are currently unsigned. The first downloadable build is x64; the source and release workflow also support native ARM64. Use an installer matching Windows Explorer's architecture. Windows 11's modern stock taskbar is the primary target; the legacy acrylic backend also supports Windows 10 build 17134+ and early Windows 11. Single-line clock, speaker, chevron, and border adapters require supported Windows 11 XAML layouts.

## Menu

| Row | Behavior |
| --- | --- |
| acrylic | Cycles the right-side status through `system`, `pure`, and `off`. Startup uses `system`: white tint in light mode, neutral dark tint in dark mode. Pure has no tint. |
| date | Toggles the date in the single-line clock, with `on` or `off` at the right and the matching calendar icon. |
| cursor | Custom choices stay grey until you import your own full Cursor Concept 3 HDPi ZIP. Default (system) then follows Windows light/dark mode. Disable always uses Microsoft cursors. Unlock colors opens a text guide and your import folder. |
| light mode / dark mode | Changes your Windows system and app mode. |
| v0.14.1 | Shows your installed version. Select it to check GitHub for a newer stable version. When it says `available`, select it again to download the installer. |
| exit | Stops jellyflower and restores the shell appearance. Login startup remains enabled. |

The clock reads `Mon Jun 22  9:41 am`, with lowercase am/pm, retaining Windows' calendar button and tooltip. The clock follows the native Windows system font. Custom fonts and the clock/font submenus have been removed; old font selections are ignored. On supported Windows 11 layouts the speaker glyph is hidden while network, battery, and Quick Settings remain; Win+A still provides the volume slider. The show-hidden-icons chevron uses 60% opacity. An active acrylic mode hides the taskbar's top border.

The tray uses the supplied filled flower silhouette with a transparent center and background: black in light mode, white in dark mode. The application and installed Start shortcut keep the rounded flower tile with inverse light/dark variants. Windows caches shortcut icons, so their visible refresh can lag. The icon embedded in an EXE's file metadata is static; Explorer uses the supplied default artwork there.

On Windows 11 build 22621 or newer, every menu level requests Windows' desktop acrylic backdrop, showing a softly blurred view of what is underneath. Text and icons remain opaque. High contrast and unsupported systems use a solid menu; Windows can also make the acrylic solid when transparency is disabled or energy saver requires it.

## Optional cursor pack

The cursor pack is licensed separately and is **not bundled**. A fresh install keeps your configured Windows cursor. All custom choices remain disabled until a complete pack is imported; disable is always available.

Choose **cursor > unlock colors** to open a text guide and `%LOCALAPPDATA%\jellyflower\cursors` in Explorer. Get your own full ZIP from [JepriCreations](https://jepricreations.com/products/cursor-concept-3-hdpi), then drop the original ZIP into that folder. Any ZIP filename is fine. Keep jellyflower running; it checks within ten seconds. Do not extract it or run its CMD/INF installers. The free 32px light/dark ZIP is a different pack and does not unlock the full color set.

Importing enables the choices without switching your cursor. Select a color or **default (system)** afterward. Jellyflower validates all 153 files and imports only the 17 cursor roles for each of the nine colors. Failed or incomplete imports leave the active pack and cursor alone. Your ZIP and imported files stay in your user folder, survive updates/uninstall, and are never uploaded. Removing them is a separate manual action.

The 17 standard cursor roles, including animated busy cursors, are applied temporarily without overwriting your saved Windows cursor scheme. The menu SVG uses the selected cursor's actual body and outline colors. Earlier explicitly configured pack locations remain supported; advanced users can configure a complete extracted pack with:

```powershell
.\jellyflower.exe --cursor-pack "C:\path\to\cursor-concept-3-hdpi"
```

Cursor and date preferences retain the internal `HKCU\Software\AcrylicTaskbar` key for compatibility with earlier builds.

No cursor pack files are distributed in the app or source archive, or installed into Windows directories. Only a user's own ZIP is extracted into their private jellyflower data folder. Apps drawing their own cursors may ignore system cursor changes. If jellyflower is force-ended, reload the saved Windows scheme with `jellyflower.exe --restore-cursors` after stopping it, or sign out.

## Updates

Releases are hosted at [mochimilk/jellyflower](https://github.com/mochimilk/jellyflower/releases). Checks happen only when requested and run in the background. The menu reports the actual result, including offline/API errors, without system notifications. The closed package becomes a rotating refresh icon while checking, and an open package when an update is available. It compares numeric stable versions and offers only this repository's matching architecture installer. No account or token is required by the app. Downloading and running a new installer updates login startup and the shortcut while preserving preferences. See [UPDATES.md](docs/UPDATES.md) for publishing and security details.

Each setup stages a separate version folder before stopping the old controller. Windows pins the Explorer helper until Explorer exits; keep old version folders until then. Controller/menu-only changes take effect immediately. **Upgrading to 0.14.1 requires one sign-out/sign-in or Windows Explorer restart to load the new taskbar tint.** Setup detects an older loaded helper and displays this reminder. The taskbar briefly disappears during an Explorer restart. Older incompatible v1–v8 helper mappings are rejected; this build uses v9 so it cannot silently reuse the old material implementation.

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

jellyflower is a small native Windows taskbar utility.

- Acrylic with system, pure, and off modes.
- A flower tray icon and lowercase menus that follow Windows light/dark mode.
- Cursor colors and a single-line clock with date and font choices.
- A normal per-user Windows installer, login startup, and an uninstall entry.
- “check for updates” compares this version with the latest public GitHub release.

Download **jellyflower-setup-x64.exe**, then double-click it. You do not need Visual Studio, PowerShell, or administrator rights. Quit TranslucentTB first. Use Windows Settings > Apps > Installed apps to uninstall jellyflower.

The first download is x64. The build workflow supports native ARM64 releases too. The cursor pack and optional Playground/Pangaia fonts are licensed separately and are not included. On another computer, these options require your own local files; Windows cursors and Segoe remain available.

Windows can use a solid acrylic fallback during battery saver even while jellyflower stays running. The Explorer helper uses internal Windows taskbar structure, so shell updates can affect compatibility. If upgrading the helper's behavior, sign out or restart Explorer to unload the previous helper.

The installer is currently unsigned. The source for this release is included and licensed GPL-3.0-or-later.

jellyflower 0.20.0

The taskbar menu has a new **tray** row. Switch it from `all` to `jellyflower` and the notification area shows only the flower: other app icons, the show-hidden-icons chevron, the language/input indicator, the microphone and location indicators, Quick Settings (network, volume, battery) and the clock disappear. Switch it back, or exit jellyflower, and Windows' own tray returns exactly as it was. The choice is remembered.

Windows' own shortcuts are left alone: Win+A for Quick Settings, Win+N for notifications and the calendar, and Win+Space for input language. The show-desktop corner is left in place.

The flower needs to be on the taskbar itself for the chevron to go. If it lives under show hidden icons, turn it on in **Settings > Personalization > Taskbar > Other system tray icons**. Until then jellyflower keeps the chevron, so its menu is always one click away.

Explorer helper v19 carries the new tray styling; Start/Search helper v20 is unchanged. A normally stopped 0.19.0 helper restores its styling before v19 attaches. Windows may keep the older helper's diagnostics connection, so if the taskbar styling does not apply after updating, sign out and back in once. Updates do not restart Explorer.

The row is unavailable on legacy taskbars without the Windows 11 XAML tray. Unfamiliar tray layouts are left alone.

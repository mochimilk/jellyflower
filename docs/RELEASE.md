jellyflower 0.19.0

jellyflower now follows your Windows display language in English, French or Korean. Menus, cursor colors, greetings, update/error messages, tray tooltip, cursor import guide and installer dialogs all use the selected language. No extra language switch or translation download is needed. Other languages use the first supported Windows display-language fallback, then English.

The single-line clock now follows Windows regional settings for month and weekday names, date order, 12/24-hour time and day-period markers. French and Korean greetings preserve your name and use the appropriate wording and order. Windows still supplies native app names, user information and calendar behavior.

Long menu labels fade at the edge and scroll like a ticker on hover. Labels that fit stay still, leaving a row stops the timer, and reduced motion keeps text still. Translated labels and secondary states are measured at the current display scale, and the ticker uses cached text layouts and repaints only its row.

Explorer helper v18 and Start/Search helper v20 load the new clock and language behavior during compatible in-app upgrades. Normally stopped compatible earlier helpers restore their styling before attachment; unknown or live incompatible helpers remain guarded. Windows may still retain an older diagnostics connection. If the taskbar does not apply after updating, sign out and back in once. Updates do not force an Explorer restart.

See docs/LOCALIZATION.md for supported languages, regional-format behavior, validation and limitations. The app does not change your Windows language, keyboard layout or regional settings.

# jellyflower installer and updater checks

Run these on the normal Windows desktop. The build environment's extract/JSON checks do not prove desktop registration or rendering.

1. Quit TranslucentTB and any other taskbar modifiers. Double-click jellyflower-setup-x64.exe on x64 Windows. Confirm installation succeeds without elevation, the flower appears under show hidden icons, and the Start menu shortcut launches the same instance.
2. Confirm Windows Settings > Apps > Installed apps lists jellyflower with the correct version. Task Manager > Startup apps should have one jellyflower entry; upgrading the earlier AcrylicTaskbar build should remove its duplicate Run registration.
3. Sign out/in. Confirm jellyflower starts with acrylic system, retains saved cursor/clock choices, and the actual taskbar looks correct. Enable battery saver, check the process remains running, and separately record whether Windows supplies blur or a solid fallback.
4. Toggle Windows light/dark mode. The tray uses black/white ink and the app/Start shortcut uses the inverse icon variant. Explorer's static EXE-file icon remains the supplied default. Windows may cache the Start shortcut's icon briefly.
5. Open the menu and select check for updates. Confirm checking appears immediately and other rows still respond. A matching published version should say up to date. Disconnect the network and retry; it should show try again, not up to date. Close the app while a request is pending; it should exit immediately.
6. For a genuine later release, publish a larger numeric stable version with the native architecture installer. Confirm available appears. Selecting the row again should open only that repository's installer download in the default browser. Canceling the browser download must leave the current app running. Draft/prerelease versions must not be offered.
7. Run the later installer. Confirm it stages a separate version folder, stops the old controller, registers the new path, and preserves clock/cursor preferences. Restart Explorer/sign out if helper behavior changed. Do not infer a helper reload solely from the newer controller version.
8. Choose exit. Confirm clock/calendar/tooltip, speaker, chevron, taskbar material/border, and configured Windows cursor scheme restore. Relaunch from Start. Uninstall from Installed apps and confirm login startup and the shortcut are removed. Windows may retain pinned helper files until Explorer exits.
9. On a computer without the optional cursor pack, a fresh launch should preserve its configured Windows cursors quietly. Missing font choices should report an unavailable file and leave Segoe readable. No personal cursor/font files should appear in setup or source archives.

See [TESTING.md](TESTING.md) for taskbar, cursor, menu, font, multi-monitor, focus, and calendar checks, and [VALIDATION.md](VALIDATION.md) for the checks completed in development.

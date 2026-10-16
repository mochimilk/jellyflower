jellyflower 0.18.0

Pure acrylic adds adaptive black/white clock and monochrome notification-icon foregrounds. Native colored logos, charging/warning states, high contrast, and unsupported renderers keep their original artwork. Standard transparent monochrome third-party tray bitmaps are supported without an app allowlist. The compositor averages a local backdrop neighborhood; no desktop capture or CPU screen readback is used. Bitmap artwork checks are bounded to four per second, cache unchanged masks, and pause when all taskbars are hidden or the session is locked. Moving content near the black/white threshold can still cause visible color changes.

Auto hide now saves the Windows account preference as well as updating Explorer's live state, preserves unrelated shell settings, verifies the change, and rolls back denied persistence. Unknown shell records are refused. It does not hide taskbar windows or enforce an app-specific preference, and the chosen Windows setting remains when jellyflower exits.

Explorer helper v16 uses a unique DLL name. Normally stopped/restored v15 helpers can retire before attachment without a sign-out. An unknown or live incompatible helper still requires signing out/back in. Start/Search v19 behavior is unchanged.

See docs/ADAPTIVE_CONTRAST.md for implementation limits and real-desktop validation steps. Native fixtures cover restoration, colored icon mutations, bitmap caching/throttling, visibility/owner cleanup, and saved auto-hide round trips; visual correctness on every third-party application or Windows build cannot be guaranteed.

jellyflower 0.16.1

- Menu text uses Segoe UI Regular at 14 effective pixels, scaled for Windows display DPI. This replaces Segoe UI Variable Text. Acrylic backgrounds, spacing, icons, and entrance motion are retained.
- Fix Element 2 not appearing over Wi-Fi/Ethernet: recognize actual taskbar MobWifi/SysWifi/TVMonitor glyphs and traverse the real IconView template, including ContentPresenter and TextIconContent. Identify network by its glyph rather than position. Offline, warning, connecting, airplane, and unknown states keep Windows' native icon. Quick Settings click/tooltip, status text, and battery are preserved.
- Make both system acrylic themes more transparent: light tint 0.58, dark tint 0.34, brightness-layer opacity 0.72 (previously 0.70/0.46/0.88). Legacy alpha is 184/255 instead of 224/255. Pure remains untinted. The brightness layer still protects legibility over extreme wallpaper colors.
- Add the last detected numeric network glyph code to diagnostics, without recording network names.

This release uses Explorer helper v11. After upgrading from an earlier running version, sign out and back in once to load the corrected network adapter and lighter taskbar tint. Menu typography applies immediately. Jellyflower does not restart Explorer; older helpers are rejected until Explorer reloads. Fresh installs need no extra step. Saved settings and imported cursor files are retained.

Install from jellyflower's version row after publication, or download jellyflower-setup-x64.exe. Windows can impose a solid material fallback. The installer is unsigned. Corresponding GPL source and third-party notices are included. See VALIDATION.md and INSTALLATION_TESTS.md for completed checks and remaining desktop checks.

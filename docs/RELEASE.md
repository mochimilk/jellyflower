jellyflower 0.16.0

- Menus use Windows 11's installed Segoe UI Variable Text, 14 effective pixels, regular weight. The fallback uses the Windows menu face. Grayscale antialiasing keeps text readable on acrylic without bold labels.
- Menus keep a 12 DIP inset from the screen/work-area edges, fixing missing right spacing and the small bottom gap.
- The main menu rises 32 DIP over 250 ms with Microsoft's documented Direct Entrance cubic-bezier(0,0,0,1). Windows animation preferences and high contrast disable this motion. This is a Fluent-matched implementation; the exact shell flyout timing is not publicly documented.
- Connected Wi-Fi/Ethernet glyphs use the supplied Element 2 vector, following the shell's foreground color. No-internet, warning, connecting, and unknown states retain Windows' own glyph. Quick Settings click/tooltip/accessibility, native spacing, and the battery remain intact. Exit restores the native icon.
- System taskbar acrylic is a little stronger: light tint 0.70, dark tint 0.46, luminosity 0.88; legacy opacity 224/255. Pure remains untinted.

This release updates the Explorer helper to v10. After upgrading from an earlier running version, sign out and back in once to load the new network icon and tint. Jellyflower does not restart Explorer. Controller/menu changes take effect immediately; the older helper is rejected until Explorer reloads. Fresh installs need no extra step. Saved settings and imported cursor files are retained.

Download jellyflower-setup-x64.exe, or install from the existing version row in jellyflower after publication. Windows may impose a solid material fallback. The installer is unsigned. Corresponding GPL source and third-party notices are included. See VALIDATION.md and INSTALLATION_TESTS.md for completed checks and remaining desktop checks.

jellyflower 0.16.2

- Menu typography now inherits Windows' menu family, size and weight, with no hardcoded font, size or weight. If the menu-metrics query is unavailable, Windows' own GUI font is used.
- Replace the GDI text coverage pass with native DirectWrite/Direct2D text rendering, including natural text layout, native alpha and display scaling. Text measurement and icon alignment use the same renderer. Grayscale smoothing preserves translucent backgrounds without ClearType color fringes. GDI remains a readable fallback if graphics initialization fails. Translucency, spacing, icons, and entrance animation are retained.
- Fix the remaining Wi-Fi/Ethernet Element 2 replacement failure: Quick Settings normally places ContentGrid directly under ContainerGrid. The previous selector mistakenly required an extra ContentPresenter. Both direct and optional presenter layouts are now supported. The regression fixture failed against the old selector before the fix and covers both structures afterward.
- Native offline/airplane/warning/connecting icons, Quick Settings click/tooltip, theme-colored artwork, measured spacing, and exit restoration are retained. System acrylic opacity is unchanged from 0.16.1.

This release uses Explorer helper v12. After upgrading from an earlier running helper, sign out and back in once to load the corrected network adapter. Menu typography applies after relaunch. Jellyflower does not restart Explorer automatically. Fresh installs need no extra step. Saved settings and imported cursor files are retained.

Install from jellyflower's version row after publication, or download jellyflower-setup-x64.exe. The installer is unsigned. Corresponding GPL source and notices are included. VALIDATION.md separates native fixture/build checks from real taskbar acceptance checks; see INSTALLATION_TESTS step 23.

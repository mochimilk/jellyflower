jellyflower 0.15.0

- Updates now download, verify the GitHub asset's SHA-256/size, install quietly, and relaunch directly from jellyflower. The menu spinner continues throughout; no browser or update notifications. Failed downloads keep the app running, and setup attempts to restore/relaunch the prior installation after handled replacement errors.
- Fixed recurring Windows error 170: finished setup dialogs no longer retain an installation lock. An actively working installation still blocks another.
- Menus use explicitly translucent native acrylic. One composed foreground surface preserves icons and text; consistent text coverage corrects rough edges and heavier-looking dark labels. The face remains 10pt regular Segoe UI, without bold labels.
- `system` is first, with `light`, `dark`, or `auto` at the right. Auto asks Windows for location permission, calculates sunrise/sunset locally, and changes system/app mode while jellyflower runs. Denied/unavailable location leaves the current theme alone.
- Before cursor import, the main menu shows `colors / unlock` with a cycling cursor-color preview. Importing your complete ZIP changes it to the cursor submenu in place. The redundant unlock submenu row is gone; no cursor pack is bundled.
- Date remains directly below acrylic. Tray/app artwork, single-line clock, taskbar tint, cursor behavior and the v9 Explorer helper remain compatible with 0.14.1.

Download **jellyflower-setup-x64.exe** and double-click it once to move from 0.14.1 or earlier. Future releases can install from the menu. A pre-0.14.1 helper loaded into Explorer still needs one sign-out/sign-in to update its material; this release does not restart Explorer. Saved preferences and your imported cursor pack are retained.

Windows can impose solid material fallback through transparency/accessibility/power policy. Auto sunrise/sunset is approximate and depends on Windows location availability; manual light/dark always remains available. The installer is unsigned. Corresponding GPL-3.0-or-later source is included. See the validation record and installation tests for completed checks and desktop checks still required.

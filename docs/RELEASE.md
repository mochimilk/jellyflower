jellyflower 0.14.1

- Menus and nested menus use Windows 11 desktop acrylic, revealing a softly blurred view of what is underneath. Text and icons retain their opacity; high contrast and unsupported systems use a solid surface.
- Date is directly below acrylic in the main menu, with on/off at the right and matching calendar icons. The clock/font submenus and custom-font loading/rendering have been removed; the clock uses the Windows system font.
- The tray button uses the supplied filled flower silhouette with a transparent center/background, black in light mode and white in dark mode. The app and Start shortcut keep their rounded tile icon.
- The update row shows your installed version, v0.14.1. Select it to check; the package/spinner/open-package states and menu-only results still work.
- System acrylic now has an explicit brightness layer in both themes, correcting the zero-luminosity white tint request that could stay dark over black wallpaper. Pure remains untinted. Legacy tints also have stronger contrast.

Download **jellyflower-setup-x64.exe** and double-click it. After updating from an earlier loaded helper, sign out and back in once (or restart Windows Explorer) to load the new taskbar material. Setup displays this reminder when needed. Existing cursor/clock preferences are retained; cursor packs are not bundled.

Windows may still replace acrylic with a solid fallback under energy saver or transparency/accessibility policy while jellyflower remains running. Native brush/property, modeled contrast, menu alpha/DPI and installer extraction checks passed; final material appearance on the affected black-wallpaper computer needs desktop confirmation. The installer is unsigned. Corresponding GPL-3.0-or-later source is included.

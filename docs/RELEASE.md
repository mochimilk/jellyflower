jellyflower 0.16.16

General now includes jellyflower wallpaper, with the supplied Lucide wallpaper icon and an on/off status. The supplied light and dark wallpapers are embedded in the executable and follow the Windows system color preference, including sunrise/sunset auto mode. The option starts off and stays enabled across exits, updates and login.

Wallpaper work runs on one sleeping background worker, woken by actions, theme and display changes. Before applying, the app saves the current images for each monitor and their placement. Disabling restores them; an outside wallpaper choice is respected and stops following. A failed apply attempts rollback, and failed restoration retains its recovery record. Errors stay inside the menu. The longer row sizes to the Windows menu font without changing its typography.

Restore covers current images/placement; Spotlight/slideshow rotation may need to be reselected in Windows Personalization. Disable wallpaper before uninstalling to restore the previous background. No desktop wallpaper was changed during validation; production logic, asynchronous worker, storage and exact asset bytes are checked with simulated desktop setters plus read-only Windows capture.

Update through general's version/download row. This is a controller/menu/assets change: Explorer v14 and Start/Search v13 identities are unchanged. Users already on 0.16.15 need no additional sign-out for this release. The x64 installer, portable build and complete source are included.

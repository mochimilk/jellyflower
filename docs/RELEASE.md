jellyflower 0.17.0

The main menu now groups acrylic, date and the new auto hide toggle under taskbar, using the supplied panel-bottom icon. Auto hide changes Windows' own preference and is greyed out for non-bottom or unavailable taskbars. Acrylic still cycles system / pure / off; date and auto hide show on / off at the right. Settings keep the menus open.

Wallpaper now has its own main-menu branch: jellyflower on / off, then theme system / auto. System is the default. Auto follows sunrise and sunset independently of Windows color mode, using the existing daily solar calculation and one-shot scheduling. Its location/timer work stops while wallpaper is off, and location is requested only on an explicit action while enabled. If location is unavailable, wallpaper follows the current system preference. Existing wallpaper backups and enabled state survive the update.

General now contains GitHub, the version/update row, a divider, and exit. Native typography, acrylic menus, icons, cursor behavior and click/hover dismissal remain. Update through general > version > download. Start/Search stays v19 and Explorer stays v15; users on 0.16.17 or newer need no additional sign-out. Native settings, solar timing, menu/DPI, wallpaper recovery, setup and updater checks accompany the x64 installer, portable build and matching source.

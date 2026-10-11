jellyflower 0.16.5

Move version/update and exit into a general submenu with the supplied settings gear. Exit uses accessible red text in both themes, respecting Windows contrast-theme colors. An offered update now says download. Live results and the animated spinner stay inside general; settings menus remain open while changing preferences. Menu text still inherits Windows menu settings.

Apply matching light/dark acrylic and border styling to the native Windows 11 Start menu. A separate small XAML helper changes only the outer background/overlay/border and restores original values and bindings on exit or controller loss. It shares jellyflower menu tint/stroke colors and respects disabled transparency/high contrast. Start's layout and contents remain native. XAML and DWM blur pipelines differ, so an exact pixel match is not guaranteed; compositor/battery policies and future Windows layouts can affect the material.

Fast login startup and taskbar helper v13 are retained. Updating from 0.16.4 takes effect after automatic relaunch without restarting Windows, Explorer or Start. Update through the existing version row; after updating, find it under general. The installer and matching source are included. See VALIDATION.md, START_MENU.md and INSTALLATION_TESTS steps 27-28.

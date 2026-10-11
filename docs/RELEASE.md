jellyflower 0.16.6

General now contains github, the installed version/update row, then a divider and red exit. The GitHub mark uses the official Octicons SVG. The supplied Lucide external-link icon appears at the right on hover and keyboard focus. The link opens the jellyflower repository in your default browser. Menu font settings are unchanged.

Click an open general/cursor parent row to collapse its submenu. It stays closed until the pointer leaves and hovers back, or you explicitly reopen it by click or keyboard.

Fix the overly opaque Start background by replacing the stacked acrylic tint/luminosity brush with a backdrop blur and one tint layer. Light/dark tint and border colors match jellyflower's menu. Original Start styling is restored on exit or controller loss, and disabled transparency/high contrast use stock Start. A separately versioned helper supports upgrading without restarting Start or Explorer. Windows layouts and compositor behavior can vary; exact pixel parity is not promised.

Fast startup, taskbar helper v13, cursor behavior and in-app updates are retained. Update from general's version row. The installer, portable build and matching source are included. See VALIDATION.md and START_MENU.md for checks and limitations.

# Source attribution and licensing

jellyflower is distributed under GPL-3.0-or-later. The repository includes the upstream license text in LICENSE.md.

TranslucentTB is free software authored by its contributors, including Ethan Smith, sylveon, MrAksel, denosawr, and the contributors listed in the repository. Original project: <https://github.com/TranslucentTB/TranslucentTB>. Reviewed/adapted snapshot: `d4636e439865df0a1a1419db408e055740ce5c74` (release branch, 2026-08-31).

The following source informed this project's acrylic-only implementation:

- `TranslucentTB/taskbar/taskbarattributeworker.cpp`: taskbar discovery, legacy acrylic accent policy and restoration, power-saving configuration analysis.
- `ExplorerTAP/visualtreewatcher.cpp`: identify taskbar XAML backgrounds and handle visual-tree notifications.
- `ExplorerTAP/taskbarappearanceservice.cpp`: the `AcrylicBrush` / `Backdrop` approach and tracking stock fills.
- `ExplorerTAP/tapsite.cpp` and `ExplorerTAP/api.cpp`: diagnostic attachment on fresh threads and taskbar-thread hook bootstrapping.
- `Common/config/config.hpp`: evidence about the disabled-by-default battery-saver appearance override.

This project substantially simplifies and modifies those techniques. It does not include TranslucentTB's packaged app, branding/assets, menus, settings framework, RPC interface, Detours library, or general appearance service. Microsoft C++/WinRT projections and SDK import libraries are build prerequisites supplied by the Windows SDK; they are not vendored here.

This is an independent project, not a TranslucentTB release. Distributing a compiled binary requires making its corresponding modified source available under the license.

The Windows 11 clock layout was investigated using m417z's GPLv3 [Taskbar Clock Customization](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-clock-customization.wh.cpp), version 1.8.1 reviewed 2026-10-10, and its [single-line discussion](https://github.com/ramensoftware/windhawk-mods/discussions/535). Its `ApplyDateTimeIconContentStyles` documents the internal `SystemTray.DateTimeIconContent` / `ContainerGrid` / `StackPanel` structure with `DateInnerTextBlock` and `TimeInnerTextBlock`. This project's independently implemented adapter uses those element identities through the existing diagnostics helper. It adds one presentation TextBlock, retains the shell's original text bindings and clock button, and restores saved layout properties/bindings on exit. Windhawk's runtime, function hooks, clock data-model replacement, web feeds, and metrics are not included.

The speaker target structure was reviewed in Ramen Software's [Windows 11 taskbar styling guide, Hide the volume icon](https://github.com/ramensoftware/windows-11-taskbar-styling-guide#hide-the-volume-icon), 2026-10-10. The native adapter independently walks the ControlCenterButton / second ContentPresenter / IconView / Grid / Grid structure and additionally requires a volume/mute glyph from Microsoft's [Segoe Fluent Icons reference](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font). It collapses and restores only that inner grid. No styling engine or Windhawk runtime is included.

The chevron glyph hierarchy was reviewed in the same guide's [Plasma theme](https://github.com/ramensoftware/windows-11-taskbar-styling-guide/blob/main/Themes/Plasma/README.md), 2026-10-10. The independently implemented adapter validates the full ChevronIconView / ContainerGrid / ContentPresenter / ContentGrid / TextIconContent / ContainerGrid / AdaptiveTextBlock#Base / TextBlock#InnerTextBlock path. It changes only the final glyph's opacity and restores its stock property/binding; it does not include the theme, its assets, or its styling engine.

The acrylic menu icons use the user's supplied panel SVGs: `panel-bottom` for pure, a half-filled lower panel for system, and a crossed-out panel for off. These are Lucide shapes or supplied derivatives. The general row uses the supplied Lucide `settings` icon. The Windows mode row uses Lucide `sun` and `moon`; the date row uses Lucide `calendar` and `calendar-off`. The generator retains the supplied system icon's 50% fill opacity and creates monochrome light/dark Windows icons at multiple sizes. Lucide Icons and Contributors license their icons under the ISC License. The full notice is included in `assets/LICENSE-LUCIDE.txt` in source and `LICENSE-LUCIDE.txt` in the distribution. See <https://lucide.dev/license>. The cursor menu geometry is the SVG supplied directly by the user, stored in assets/cursor-menu.svg and represented as the same native cubic path. Runtime body/border colors are read from the user's external cursor files; no cursor-pack image or outline trace is embedded.

Cursor Concept 3 HDPi is an external cursor pack by **JepriCreations**, [website](https://jepricreations.com), [DeviantArt](https://www.deviantart.com/jepricreations). Its supplied Agreement.txt permits personal use and modification but prohibits distributing the pack files. This project imports the user's own complete ZIP into their private jellyflower data folder, or references an explicitly configured local pack. It does not include, relicense, modify, or redistribute cursor files in its source or binary packages. The cursor names identify the original pack's variants. The supplied install.inf files informed the mapping of file names to the 17 Windows cursor roles; they are not executed or bundled.

The notification-area and application icons are the jellyflower SVG artwork supplied directly by the user. Their original paths are retained in assets/tray.svg and assets/app.svg, with light/dark icon variants generated at multiple sizes. The previous Y2K Element 48 artwork is no longer included. The update row uses Lucide's package, package-open, and refresh-cw icons under the included ISC notice.

## miniz

The ZIP reader/inflate sources are miniz 3.1.2 from https://github.com/richgel999/miniz/tree/3.1.2, statically linked with writing/deflate and zlib compatibility APIs disabled. `miniz_export.h` is a local static-link definition. Upstream sources retain their MIT license:

Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Start-menu target research

Start AcrylicBorder/AcrylicOverlay names, the overlay rule, and the diagnostics approach were reviewed in Ramen Software's [Windows 11 Start menu styling guide](https://github.com/ramensoftware/windows-11-start-menu-styling-guide) and [Windows 11 Start Menu Styler source](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/windows-11-start-menu-styler.wh.cpp), authored by m417z and contributors (GPL-3.0), on 2026-10-11. Jellyflower's small independent adapter reuses its existing TAP pattern and does not bundle Windhawk's runtime, general styling engine or theme layouts. The effective-background workaround follows the documented source observation that AcrylicBorder's ReadLocalValue can return a different brush. See docs/START_MENU.md for implementation details and sources.


## GitHub Octicons

The GitHub mark SVG comes from https://github.com/primer/octicons/blob/main/icons/mark-github-16.svg. It identifies the external project repository link. GitHub is a trademark of GitHub, Inc. The icon is licensed as follows:

MIT License

Copyright (c) 2026 GitHub Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

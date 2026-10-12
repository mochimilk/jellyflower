jellyflower 0.16.14

Cursor hover previews now load just the hovered arrow and reuse it within the submenu, instead of repeatedly replacing all 17 Windows cursor roles. Clicking still applies the full set. Menu hover redraws cover only the changed rows, text layouts are reused, and unused cursor-color artwork and spinner initialization are skipped when opening the main menu. The existing appearance and interactions remain.

Start now refreshes its cached pinned/recommended header measurement when the page returns onscreen or its content changes. This addresses the intermittent blank gap below search without resetting the scroll position or adding an idle layout loop.

The separate Windows Search results menu now has the same 20px outer corners as Start, with matching inset background and shadow corners. Its filter tabs have 12px more room below the search divider. Native typography, acrylic, editing and results stay intact.

Update through general's version/download row. The x64 installer, portable build and matching source are included. The native reopening/resize/restoration fixtures cover the new behavior; intermittent issues in private Windows shell layouts still need desktop acceptance checks.

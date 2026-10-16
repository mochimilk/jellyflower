jellyflower 0.18.1

Fixes the flower tray icon flickering and jumping in pure acrylic. Routine refreshes no longer resend unchanged tray artwork. Identical Explorer bitmap replacements and ImageOpened events retain the decoded contrast mask, overlay and native image opacity instead of briefly exposing the original icon and rebuilding its replacement. Colored status artwork, theme/DPI changes and Explorer restart recovery remain supported.

Explorer helper v17 uses a unique DLL name so an update loads the corrected code. Normally stopped v15/v16 presentation is synchronously restored before attachment; live or unknown incompatible helpers remain guarded. Start/Search styling and the saved Windows auto-hide setting are unchanged.

Native regressions cover 100 redundant tray refreshes, repeated identical bitmap replacements without mask rebuilds, theme/DPI changes, shell recovery, colored mutations, input/layout, binding preservation and safe helper retirement.

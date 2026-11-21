jellyflower 0.21.0

The menu has a new **keyboard** row. Switch it from `system` to `symmetric` and Windows' docked touch keyboard is rearranged like a tablet keyboard, with small gaps between keys, rounder corners and a little padding around the edges. Every key still responds across its whole width, and Windows' own colors are kept in light and dark mode.

On a narrow keyboard (a tablet in portrait), the letter page uses a minimal layout: ten letters across the top, the next row half a key in, shift and delete around z to m, and &123, the language key (a globe instead of ENG), Ctrl, space, microphone and return along the bottom. The &123 page follows the same frame. Punctuation, the second shift, Tab, Home, End and the arrow keys are hidden there. Wider keyboards keep every key, with rows that stagger like a hardware keyboard while both edges stay flush.

The return key dims and ignores taps while the text box is empty, and turns your accent color once there is something to send. Password boxes and apps that don't report their text keep the normal return key. Emoji, split and traditional layouts stay as Windows draws them; choose Default in the keyboard's own settings and the symmetric layout comes back. Set the row back to `system`, or exit jellyflower, and Windows' layout returns right away.

Start now fits screens that are too narrow for it, such as a tablet in portrait. Instead of being cut off at the right edge, the menu fills the width of the screen with four pinned columns, and returns to Windows' size when you rotate back.

Start/Search helper v21 carries both changes. A normally stopped 0.20.0 helper restores Start and Search before v21 attaches, so no sign-out is needed. The Explorer helper is unchanged at v19. jellyflower only connects to the touch keyboard after you choose `symmetric`. Updates do not restart Explorer.

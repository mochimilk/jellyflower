jellyflower 0.16.19

The Start account badge returns to its native left/right padding for alignment with the rest of Start. Top/bottom padding now matches those original side values. The 28-DIP avatar and rounder 10-DIP hover container remain, with auto-sized footer hosts to fit the taller badge. The redundant account-name hover tooltip is removed. Native account actions, typography and keyboard behavior remain.

Start and Windows Search now show `search <PC name>` instead of the heart phrase, preserving the Windows device name's case. The desktop controller reads the active device name once and supplies it to both shell helpers; there is no repeated menu-time lookup. Typed queries and selection are unchanged. If Windows requires a restart to finish a PC rename, the new active name appears after that restart. Original shell placeholder, padding, tooltip and sizing values/bindings restore on exit or controller loss.

Update through general's version/download row. Start/Search v15 waits for older helpers through v14 to restore before connecting. Explorer remains v15; taskbar acrylic and other settings are unchanged. Users already running 0.16.17 or 0.16.18 need no additional sign-out. The x64 installer, portable build and matching source are included.

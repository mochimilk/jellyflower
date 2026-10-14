jellyflower 0.16.24

Fixes wallpaper failures caused by stale Windows wallpaper-service connections after Explorer restarts. Jellyflower reconnects for a failed capture, with one bounded retry. Wallpaper setters are not blindly replayed after partial failures, and saved recovery information is retained when restoration cannot finish.

Old disconnected displays no longer block enabling because their assigned image is missing; their assignments are left alone. Connected screens are still backed up before changing anything. A removed or inaccessible original can use a different readable current-image path reported by Windows only when Windows confirms that it is the common wallpaper. If no safe copy exists, enabling stops without changing the desktop. Disabling can use the untouched saved backup when the original is now missing or locked.

The wallpaper row now shows useful inline errors such as image missing, file in use, not allowed, or the Windows error code. The row remains clickable to retry, and diagnostics include wallpaper status/result. No wallpaper polling timer is added.

Update through general > version > download. Start/Search stays v19 and Explorer stays v15; no extra sign-out is needed from 0.16.17 or newer. The x64 installer, portable build and matching source include wallpaper recovery, menu/DPI, setup recovery and updater checks. A specific laptop's original unreadable image cannot be identified from older logs; a missing current image with no safe backup still requires selecting a readable Picture in Windows Personalization before enabling.

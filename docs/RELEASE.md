jellyflower 0.17.1

System auto and wallpaper auto no longer retain a location object or status listener between readings. Each one-time city-level reading has a 15-second timeout, releases its resources on success/failure, and is cancelled when auto is disabled or its owner closes. Sunrise/sunset calculations use saved rounded coordinates between brief activation/startup and daily refresh readings. A transient refresh failure can retry after 15 minutes; it does not leave a listener running.

Windows permission can remain granted, and its global Location services switch remains under your control. Jellyflower never switches that setting on or off. Revocation is discovered at the next read rather than through a persistent watcher; cached daylight calculations may continue until then. Wallpaper auto stays independent of system auto and does no location work while wallpaper is off.

Update through general > version > download. Menus, wallpaper recovery data and helper generations stay unchanged from 0.17.0. Production-coroutine lifetime/cancellation tests use a simulated location provider, with no real location access or settings changes. Installer/source/portable checks and production update verification accompany the x64 release.

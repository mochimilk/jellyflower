jellyflower 0.17.2

Fixes wallpaper error 80004005 caused by a disconnected display assignment returning E_FAIL instead of S_FALSE. Only that generic rectangle failure is checked against Windows' active-display inventory; unavailable inventory and other errors still stop safely. No disconnected screen is added to the apply/backup targets.

A single attached screen can now preserve Windows' public current-picture copy when its original source has disappeared, including when stale assignments make the common-image query return S_FALSE. Multiple attached displays still require a confirmed matching common source. If neither the source nor a safe current copy is readable, the original is protected and the operation stops. This covers a missing-image recovery gap; the other laptop's specific missing path has not been inspected.

Wallpaper diagnostics now record the failing operation without image paths. Missing extracted jellyflower images are re-created from bundled resources, with no download. Existing recovery records, original backups, independent system/wallpaper auto themes and bounded location ownership remain compatible. Start/Search v19 and Explorer v15 are unchanged; this update needs no additional sign-out.

Native wallpaper regressions use simulated setters and workspace files. Read-only validation on the affected desktop confirms capture now succeeds; automated tests do not change its wallpaper. Exact installer/source/portable payload checks accompany the x64 build.

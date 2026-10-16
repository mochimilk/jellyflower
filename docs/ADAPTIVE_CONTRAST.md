# Adaptive taskbar contrast and native auto hide

## Scope

The Windows 11 XAML backend enables adaptive foregrounds only in pure acrylic. System/off modes restore the most recent native brushes and image opacity. Calendar click, tooltip, accessibility, original icon source, and routed input stay on Explorer's existing controls. A uniquely named v17 helper prevents Windows from reusing an old pinned implementation. Normally stopped v15/v16 helpers are synchronously restored before attachment; unknown or still-running generations require signing out/back in.

The clock and native neutral text/path glyphs use Composition backdrop brushes. Third-party `SystemTray.NotifyIconView` artwork uses CPU-accessible `WriteableBitmap` pixels. Transparent, neutral artwork (including anti-aliasing) is eligible; colored visible pixels and opaque backgrounds are rejected. There is no app-name allowlist and no public Windows flag that identifies every theme-aware notification icon. Other image source types, live glyph-color/bitmap-opacity bindings, and unusual renderers retain native artwork. Bound glyphs are excluded so future warning colors continue to update. The notification overflow already has its own readable background and is excluded.

Colored brush changes are observed directly. Bitmap polling and mask creation are capped at four times per second while pure and a taskbar is visible, including rapid source replacements. Each mask decode also revalidates the current tiny bitmap once before applying, to avoid stale colored artwork. A changed monochrome image is encoded into an alpha mask in memory; unchanged sources reuse the decoded mask. Colored changes restore the source image. Checks stop when all taskbars are hidden, the session is locked, or pure is disabled. On multiple monitors, a visible taskbar can keep the bounded bitmap check active for other tray instances too. No desktop capture, screen pixel readback, location use, or file logging of icon artwork is involved.

## Material limits

Since 0.18.1, routine app refreshes leave unchanged tray artwork alone. Explorer source/open events compare the tiny CPU bitmap before discarding a contrast layer; identical pixels and dimensions reuse its existing overlay and decoded mask, including a pending decode. Those event comparisons are additional to capped polling, but create no new PNG/surface for duplicate artwork. Real colored changes still restore native pixels immediately. No repeated replacement of the flower or its mask is used to maintain contrast.

The compositor approximates sRGB linearization, averages a 16-DIP Gaussian neighborhood, and chooses the black/white contrast crossover near luminance 0.179. This is spatially weighted, not an exact average of the whole taskbar corner. Shapes can span differently colored pixels; moving content near the threshold can cause a color change or flicker. It has no temporal hysteresis. Per-glyph GPU effects cost more than fixed-color brushes, especially over moving video. It is not a measured battery-life improvement; native clock/liveness timers still exist.

Backdrop behavior depends on Windows' compositor and shell version. Windows can suppress acrylic under battery saver, transparency settings, high contrast, remote sessions, or graphics limitations. High contrast keeps native foregrounds. Unsupported effect/source/layout paths retain or restore native appearance instead of consuming input or adding opaque rectangles. A successful API call or nonzero diagnostic count alone does not prove visual contrast; visually test real overlapped content.

## Auto-hide implementation

`SHAppBarMessage(ABM_SETSTATE)` updates the system taskbar's live auto-hide bit, preserving other appbar flags. On this host, a native round-trip showed the live bit changed immediately while the saved StuckRects3 record remained unchanged after four seconds. The explicit menu toggle therefore validates the existing record (40/48 bytes, matching size, signature -2, bottom edge, known flags) and saves only ABS_AUTOHIDE, preserving other bytes. It refuses unfamiliar records, verifies the live result, rolls back a failed persistent write, and notifies Settings/secondary taskbars. No Explorer restart, visibility loop, or ongoing enforcement is used for the toggle. Exiting jellyflower leaves the user's chosen Windows setting intact.

The Windows settings portability reference documents the shell record as opaque; its internal layout is not a stable public API. Future Windows builds may need a new adapter. External Windows Settings changes remain authoritative. A focused taskbar menu, notification, or pointer over the taskbar can legitimately keep an auto-hidden taskbar open.

## Validation checklist

1. Choose pure. Place dark, white, mid-gray, saturated, mixed-color, and moving windows behind the clock and tray. Check the clock, network, chevron, battery, and installed monochrome third-party icons; compare against colored logos and charging/warning states.
2. Hover/click each icon, open its own menu, and open the calendar. Verify hover tooltips and native input remain unchanged. Test 100%, 150%, 200% DPI and multiple displays.
3. Change a third-party icon's state/source and Windows theme while pure is active. Check color status artwork returns unchanged. Switch to system/off and exit; confirm original colors/opacity and normal clock return.
4. Enable auto hide. Dismiss the menu and move away; reveal the taskbar repeatedly over light and dark content. Lock/unlock, sleep/resume, unplug, and enable battery saver. Confirm no stale invisible bitmap overlay or stuck glyph.
5. Toggle auto hide in jellyflower, check Windows Settings > Personalization > Taskbar > Taskbar behaviors, then reverse the choice there and reopen jellyflower. Exit jellyflower and restart Explorer/sign out to confirm the preference persists. Test secondary displays and the disabled non-bottom edge guard.
6. Run native `AdaptiveContrast`, `ClockLayout`, `BridgeNotify`, and `DesktopOptions` fixtures. The first covers brush/binding ownership, colored mutations, bitmap source changes, masks/cache/throttle, hidden-state reads, late-decode cancellation, and native layout/input. DesktopOptions uses simulated APIs for denied writes, rollback, unsupported records, and byte preservation.

## References

- [ABM_SETSTATE](https://learn.microsoft.com/en-us/windows/win32/shell/abm-setstate)
- [Windows 11 settings reference](https://github.com/MicrosoftDocs/windows-dev-docs/blob/docs/hub/apps/develop/settings/settings-windows-11.md)
- [Composition brushes](https://learn.microsoft.com/en-us/windows/uwp/composition/composition-brushes)
- [Composition effects](https://learn.microsoft.com/en-us/windows/apps/develop/composition/composition-effects)
- [Gamma transfer](https://learn.microsoft.com/en-us/windows/win32/direct2d/gamma-transfer)

# Native and laptop test checklist

Current 0.16.3 uses the Fluent Body role for menu text and native network artwork. Historical Element 2/network tests below are retired. See INSTALLATION_TESTS steps 24–25 for current typography/startup acceptance.

Current 0.14.1 clock behavior is system font only, with a date row directly below acrylic. Earlier font/submenu checks below describe retired behavior and are retained as development history; use INSTALLATION_TESTS.md steps 13–17 for current acceptance.

The x64 Release build, basic native lifecycle checks, and desktop XAML attachment on Windows build 26200 **passed**. Desktop attachment required an Explorer restart to clear a stalled session, and the user confirmed the original build's frosted effect. Tray update and battery-saver checks below remain **pending** unless recorded in VALIDATION.md. Record the Windows build, architecture, GPU/driver version, date, and whether the taskbar is stock. Windows may label the power mode Battery saver or Energy saver.

## 1. Build and basic lifecycle

1. Install the prerequisites in README.md. On x64 Windows run `Build.ps1`; on ARM64 build matching ARM64 binaries. Confirm both the executable and DLL are in the distribution and the build exits successfully.
2. Quit TranslucentTB and other taskbar appearance utilities. Enable Transparency effects and disable high contrast for the visual baseline. Run the utility as the normal logged-in user.
3. Run the scripted lifecycle test from the source folder:

```powershell
.\tests\Smoke.ps1 -ExecutablePath .\dist\x64\AcrylicTaskbar.exe
```

This test starts and stops the real utility, changes the taskbar while it runs, and checks startup-on, responsiveness, real-controller mouse/keyboard callback routing, toggle persistence across a recovery tick, duplicate-instance exit, JSON diagnostics, graceful stop, and restart without restarting Explorer. It does not change login registration. It delivers the tray callback programmatically; a pass **does not test actual shell clicks, rendering, or battery saver**.

4. Launch again and wait up to 40 seconds for initial modern attachment. Run `dist\x64\Diagnose.ps1`. Expect `running` and `responsive` true, backend `xaml` on supported modern Windows 11 or `accent` on a legacy supported taskbar. For modern XAML, bridge state 2 means attachment succeeded. `appliedBackgrounds` should match visible taskbar backgrounds after initialization. State 0 is idle, 1 starting, 3 failed. A nonzero `lastResult` is a signed HRESULT; consult the event log file.

   If state 1 and zero backgrounds persist, close other taskbar customizers and use Task Manager to restart Windows Explorer once. Keep AcrylicTaskbar running; it should rediscover the taskbar. Wait up to 40 seconds and repeat diagnostics. Closing a customizer may leave its helper loaded in Explorer. This recovery resolved the observed build-26200 stall; the precise original cause was not established. A zero HRESULT during state 1 does not mean attachment succeeded.
5. Verify frosted blur visually against a detailed desktop wallpaper or window behind the taskbar. Test Start, Search, Task View, maximized and full-screen windows. The app has no state-specific appearance changes.

## 2. Isolate battery-saver behavior

Before the saver tests, find the monochrome flower under Show hidden icons and click it to open the menu. Cycle the acrylic row through system, pure, and off, checking the current-mode label, panel icon, and tooltip. The flower stays the same shape in each mode. Click/right-click and Enter/Space activation open the menu without changing acrylic; a second tray activation closes it. Switch Windows light/dark mode and display scales: the flower recolors with the taskbar and keeps its center transparent. Restart Explorer while acrylic is off; verify the flower returns and the taskbar stays stock. Exit removes the flower and restores the taskbar. Relaunch starts in system mode. The selected mode survives power/display notifications for the current run.

For the 0.9.0 popup, verify rounded corners/shadow on Windows 11, comfortable row spacing, lowercase `acrylic (system)` / `acrylic pure` / `acrylic off`, `cursor`, `light mode` / `dark mode`, and `exit`, and no bold text. Cycle acrylic repeatedly with clicks and Enter/Space: system → pure → off → system. The same menu must stay open, updating the current-mode label and panel icon; the tray retains the flower. The cursor glyph should retain the actual arrow's proportions, centered in the same square slot as the acrylic icons. Its fill must match the chosen cursor; its outline is dark on light menus and light on dark menus. Compare both themes at the SAME display scale when judging outline weight. Check 100%, 150%, and 200%. Click the tray icon again with the root open, then with both menus open: each must close without changing settings. Open the cursor submenu, move onto acrylic or exit, and verify only the submenu collapses after the Windows menu delay. Leaving both menus also collapses the child; moving from its parent row into the child should cancel that collapse. Changing a cursor while hovering its choices must keep both menus open. Check Up/Down/Tab navigation, Escape, outside clicks, and high contrast. Open near each screen edge; both menus must stay within the work area. Theme changes must update both open menus without dismissal or focus loss. Display/scaling changes dismiss the popup, and reopening must use the new geometry. Exit closes the controller, removes the tray icon, and restores the stock taskbar promptly through the notification path. The Explorer helper remains pinned but inactive, as documented in README.md.

1. Optionally establish the original TranslucentTB baseline first: record its version/configuration, disable its battery-saver appearance override, select acrylic for each applicable state, and observe its PID before and after enabling battery saver. Close it before testing AcrylicTaskbar.
2. With AcrylicTaskbar running and saver off, record the PID in Task Manager and save a report:

```powershell
.\dist\x64\Diagnose.ps1 -OutputPath .\saver-off.json
```

3. On a laptop, unplug AC if needed and enable Battery saver/Energy saver through Windows Settings. Save `saver-on.json` using the same command with that filename. Confirm the saver field when available, and compare PID, responsiveness, applied count, and result. Keep saver enabled for five minutes and inspect both process presence and taskbar appearance.
4. Distinguish these outcomes:

| Observation | Interpretation |
| --- | --- |
| Same PID; responsive; accepted brush requests; blur remains | Desired process and visual behavior observed on this build |
| Same PID; responsive; requests remain accepted; taskbar becomes solid | Consistent with Windows material fallback; app survival requirement passes, visible acrylic does not |
| PID remains but applied count is zero or HRESULT fails | Attachment, targeting, or API failure; inspect diagnostics before blaming compositor policy |
| Process disappears or PID changes unexpectedly | Investigate crash, external termination, or relaunch; cannot classify this as normal brush fallback |

5. Disable saver. Confirm blur returns without restarting the utility. Repeat with Transparency effects off/on and high contrast on/off: a solid fallback under those settings is expected for the system brush. `visualBlurVerified` stays false in JSON because the utility cannot measure rendered blur.
6. Sign out and back in with saver already enabled after installing login startup. Confirm the process appears and stays running; record the visual result separately.

## 3. Shell and display recovery

1. With the utility running, use Task Manager's Restart command for Windows Explorer. Confirm a new Explorer PID and taskbar, the same controller PID, and restored accepted requests within roughly five seconds plus diagnostic attachment time.
2. Disconnect and reconnect a second monitor, change display scaling/resolution, enable/disable taskbar auto-hide, and lock/unlock. Confirm all taskbar backgrounds receive acrylic requests. Sleep/resume and repeat on battery.
3. Stop with `--stop`. Confirm the controller exits. On modern XAML expect prompt stock-fill restoration through the notification path; the two-second check remains a fallback. Start again without restarting Explorer to verify the retained bridge follows the new controller. On the legacy backend stock restoration is best effort via a shell composition-refresh message.
4. Force-end only AcrylicTaskbar in Task Manager. The modern bridge should restore stock fills when its process handle signals, within about two seconds. Legacy crash restoration is not guaranteed until a shell refresh/restart. Record this as a separate cleanup limitation.
5. Install, sign out/in, verify automatic launch, then uninstall. Confirm the named Run value is removed. Another copy's startup registration is intentionally protected from removal. Sign out or restart Explorer before replacing/deleting the pinned modern DLL.

## 4. Resource and negative tests

For version 0.9.0, install dist/x64-modes/Install.ps1 and restart Windows Explorer once when upgrading from any earlier version. The three-mode material uses v5; older v1/v2/v3/v4 mappings block attachment until Explorer exits. The immediate-toggle receiver changes the pinned helper. Until older v1/v2/v3 mappings disappear, the controller waits with ERROR_REVISION_MISMATCH. The v5 mapping and TAP class prevent attachment to an older helper. Wait for bridge state 2 after that restart.

1. Switch Windows mode in Personalization between light and dark with acrylic enabled. Light should have frosted blur and a white tint that makes its dark text readable over detailed/dark backgrounds; dark should keep light text readable over white backgrounds. System uses explicit TintLuminosityOpacity 0.85 in both themes; TintOpacity remains 0.65 white / 0.40 gray. Legacy alpha is 217/255 for both neutral colors. Check the taskbar top line is absent in both modes. Expect taskbarBordersHidden to be positive when BackgroundStroke exists. Toggle acrylic off: both stock fill and stock border should return while the menu stays open. Toggle on again in that same menu and use exit: verify restoration again.
2. Open cursor using hover/click or Right/Enter. Verify all nine names, default (system), and disable after a divider. Every row should be lowercase and regular weight. Check Left/Escape returns to the parent menu and another Escape dismisses it. At 100%, 150%, and 200% scaling the disable row must remain visible, with the complete submenu inside the monitor work area. Test opening near both screen edges.
3. Select several colors in succession using clicks and Enter/Space without reopening the submenu. Both menus should stay visible and the check should move immediately. Left/Escape should still return to the parent, and another Escape or a click outside should dismiss. A failed application should keep the previous choice checked and leave the menu open. Repeat with default (system) and disable. Then move across a desktop, link, text field, and resizable window. Verify arrow, link, I-beam, resize, and animated busy cursors belong to that color. Check cursorApplied true and cursorLastError 0. Some applications use their own private cursor resources; test standard Windows cursors too.
4. Select default (system), change Windows mode, and verify the pack's light/dark variant switches within the next refresh (roughly five seconds). Select a fixed color and change the theme again; the color should stay fixed. Restart the utility and sign out/in to check persistence. Acrylic still starts on independently of the saved cursor choice.
5. Select disable: Microsoft's stock cursors should appear and remain selected after relaunch. Exit/uninstall: the Windows-configured scheme should return, which may be a prior custom scheme. Re-enable a color to resume pack cursors. Keep the existing Downloads pack folder; test relocating with Install.ps1 -CursorPackPath and missing-folder error handling. Force-ending the utility may require --restore-cursors or reapplying a Windows scheme.

The optional native cursor-logic test loads the real files and bitmap data but substitutes system/registry writes, so it is safe to run alongside another cursor tool. It covers theme matching, fixed colors, all 17 roles, preference read/write logic, stock disable, exit cleanup, missing files, partial API failure, and save failure. It does not verify real desktop cursor replacement or Windows registry permissions:

```powershell
cmake -S . -B build-tests -G "Visual Studio 17 2022" -A x64 -DACRYLIC_BUILD_TESTS=ON
cmake --build build-tests --config Release --target CursorLogic --parallel 1
.\build-tests\Release\CursorLogic.exe 'C:\path\to\cursor-concept-3-hdpi'
```

Measure idle CPU and working set in Task Manager over 15 minutes, including saver on/off; no performance numbers are claimed yet. Include both the controller and the additional bridge memory in Explorer. The controller has a five-second recovery timer; each modern background has a two-second UI-thread owner check. After stop, the small pinned helper and its idle observers remain until Explorer exits.

Test a folder path containing spaces, delayed Explorer startup, missing bridge DLL, mismatched architecture, and unsupported shell structure. Diagnostics should show waiting/unsupported or failure while the controller remains running; there is no popup UI. Remote Desktop, virtual machines, third-party taskbars, and future Windows shell builds are not validated targets. An unavailable battery test on a desktop machine must be marked not tested, not passed.

## Result record

| Case | Windows build / GPU | Process result | Visual result | Notes |
| --- | --- | --- | --- | --- |
| Native x64 build | Windows 26200 / MSVC 19.44 / SDK 26100 | Passed | N/A | Release build and packaging completed |
| Basic native lifecycle | Windows 26200 | Passed | Not tested | Startup, duplicate launch, diagnostics, stop, restart; backend waiting |
| Desktop XAML attachment and original acrylic | Windows 26200 | Passed | User confirmed | State 1/zero backgrounds changed to state 2/one background after Explorer restart; same controller PID |
| Tray icons, real toggle clicks, theme/DPI | Windows 26200 | Pending | Pending | Test 0.2.0 on the desktop |
| Native ARM64 build | Pending | Pending | N/A | Must match Explorer bitness |
| Battery saver off/on/off | Pending | Pending | Pending | Real laptop needed |
| Login with saver enabled | Pending | Pending | Pending | Per-user Run entry |
| Explorer restart / multi-monitor | Pending | Pending | Pending | Stock shell only |
| Graceful stop / crash cleanup | Pending | Pending | Pending | Legacy crash cleanup is best effort |


## 6. Immediate toggles and visual alignment (0.7.0)

After the one-time Explorer restart and bridge state 2, toggle acrylic off/on several times in the same menu. The helper should process each change promptly, with no periodic two-second wait. Windows may still animate the material; record perceived delay or a screen recording on the actual desktop rather than treating notification-dispatch timing as visible latency. Both stock fill/border restoration and acrylic reapplication should follow the latest choice. Rapid alternating clicks must finish in the selected state. Exit should restore promptly. Force-ending the controller still uses the existing two-second crash-recovery check.

At the same 100%, 150%, and 200% scale, compare light and dark menus. The cursor should be slightly smaller than before, with its actual proportions, selected fill color, and theme outline intact. Acrylic icons, cursor icon, chevron, and submenu checkmarks should be vertically centered on their labels. Switch acrylic and cursor choices repeatedly; regular font weight, alignment, live labels, and submenu behavior must remain consistent.

For a native notification check without injecting Explorer, configure with ACRYLIC_BUILD_TESTS=ON, build Release, and run BridgeNotify.exe. It creates invisible windows on two native UI threads, verifies publication before asynchronous delivery, no messages for unchanged state, forced reconnect, process-ID filtering, rapid toggles, and UI-thread affinity. Its dispatch time excludes XAML/compositor rendering. A missing receiver or failed post retains the recovery timer; busy Explorer UI threads can delay message handling.


## 7. Windows mode action (0.8.0)

1. In a normal desktop session, open Windows Settings > Personalization > Colors and the tray menu. The action must read `light mode` in dark Windows mode and `dark mode` in light Windows mode, with regular text and a sun/moon icon. Click it: both the Windows and app modes should change to that choice. The same menu must remain open, recolor, and name the opposite next action. Repeat using Enter/Space at 100%, 150%, and 200% scale. Compare the icon and label alignment.
2. With the cursor submenu open, change the mode through the parent row, or change it externally in Windows Settings. Both menus should stay open, recolor, and preserve keyboard focus and the selected cursor check. `default (system)` should follow the new theme; a fixed cursor color should remain fixed. Acrylic stays in its selected on/off state. Inspect the actual taskbar tint after each switch; a successful preference write does not prove visible compositor output.
3. Start from a custom combination (system dark/apps light and vice versa). The next choice follows the current system mode and sets both to that choice. Exit and relaunch, then sign out/in: the chosen Windows mode stays saved and acrylic starts on. Apps with a fixed internal theme can ignore the app preference. Contrast themes retain their own colors; switching the base mode must not disable contrast mode.
4. Change mode repeatedly while another app is slow to respond. The menu must remain responsive. Record real shell/app transition timing separately from menu updates. Confirm accent color, transparency, and battery saver settings are preserved. Test managed policy/access-denied behavior only in an appropriate test account; a failed write must report an error and keep the menu open with the actual system mode.

The optional ThemeLogic test substitutes registry operations and broadcasts, while using a real Windows worker for notification dispatch. It does not change the desktop theme. It verifies both values, mixed modes, missing values, invalid data, open/read/write failure, rollback of an existing or missing value, rollback failure reporting, and notification-queue failure:

```powershell
cmake -S . -B build-tests -G "Visual Studio 17 2022" -A x64 -DACRYLIC_BUILD_TESTS=ON
cmake --build build-tests --config Release --target ThemeLogic --parallel 1
.\build-tests\Release\ThemeLogic.exe
```

The mode preference names are implementation details rather than a supported public theme setter. Native isolated checks cannot establish real Explorer/app theme adoption, permissions, or compositor transitions. Confirm those cases in the normal Windows account.


## 8. Three acrylic modes and cursor container (0.9.0)

After installing the prebuilt update and restarting Explorer once, open the tray menu. The initial row and tray icon must show `acrylic (system)` with the supplied half-filled panel. Click once for `acrylic pure` and its unfilled panel; click again for `acrylic off` and its crossed-out panel; click again to return to system. All clicks keep the same menu open. Repeat with Enter/Space, and with the cursor submenu open. The label describes the selected mode rather than the next action. Menu opening/closing never advances the cycle.

In both Windows themes, inspect the actual taskbar. System uses 65% white or 40% dark color tint with a separate 85% brightness layer in both themes (legacy alpha 217/255). Modern pure requests zero color and luminosity tint while retaining the acrylic material. Pure may make taskbar text less legible on some backgrounds. Both active modes suppress the modern top border; off and exit restore stock fill and border. Switch modes rapidly and change Windows light/dark mode while pure is selected: the pure request stays untinted, the glyph ink follows the theme, and the fallback color follows Windows. Diagnostics must report `acrylicMode` as `system`, `pure`, or `off`, with `acrylicEnabled` true for the first two. Relaunch and sign out/in: startup returns to system.

Windows can still substitute its solid acrylic fallback during battery saver, high contrast, disabled transparency, or unsupported hardware. A zero-tint request does not override those policies. The legacy accent needs minimum alpha 1/255 to keep acrylic, so pure there is a near-zero tint rather than a mathematically zero tint. Actual modern and legacy rendering still requires desktop validation.

At 100%, 150%, and 200% scaling, select default and several cursor colors, then disable. Inspect the cursor row: its container must remain square, centered vertically in the row, with the visible arrow centered horizontally and vertically within that container. The stock arrow is narrower than the pack arrow; preserve that actual shape and its proportions. Do not stretch it or use its hotspot as the icon center.

With ACRYLIC_BUILD_TESTS=ON, BridgeNotify.exe also checks system/pure/off cycle order, both tint recipes, zero pure color/luminosity tint, and prompt publication/delivery of pure mode. CursorIcons.exe accepts the cursor-pack path, loads real cursor files and the stock/fallback arrow, and checks 20/30/40-pixel square outputs and visible contour centers in both ink themes. It does not replace desktop cursors. Native menu captures use the production WM_DRAWITEM renderer for every row because PrintWindow alone can leave standard owner-drawn buttons blank.

## 9. Single-line clock and smaller menu glyphs (0.10.0)

1. Install `dist/x64-clock/Install.ps1` and restart Windows Explorer once after upgrading from any earlier helper. On the normal desktop, the clock should show one vertically centered line such as `Mon Jun 22  9:41 AM`, with a single-digit hour/day when applicable and no seconds. Run Diagnose.ps1: `singleLineClocks` should be at least 1, `clockLastResult` should be 0, and `bridgeState` should be 2. These are accepted-layout counts, not visual proof. Zero clocks means no supported clock was found; the stock clock should remain usable.
2. Click the actual text, the gap between date/time, and clock padding. Windows' calendar/notifications flyout must work as before. Hover to inspect the usual full-date/time tooltip. Test the clock with keyboard and screen reader navigation; the existing clock's accessible name and action should remain available. No added text element should appear as a separate focusable control.
3. Observe a minute rollover. Change time zone only in a suitable test account/VM, and verify local time updates; also cover noon, midnight, month/day changes, resume, and automatic time synchronization. Do not change the main desktop's date/time for this check. Fixed English abbreviations and 12-hour AM/PM are intentional to match the reference, independent of regional date formats.
4. Cycle system/pure/off with the menu still open. The clock must stay single-line in all three modes. Change light/dark mode and contrast theme; the clock should use the live stock text appearance. Check 100%, 150%, and 200% display scaling, Windows text scaling, multiple monitors, primary-monitor changes, Explorer restart, and taskbar auto-hide. Confirm no clipped text, excessive width, displaced notification icons, or empty second row.
5. Exit from each acrylic mode, especially off. The custom line must disappear and the original Windows date/time lines, current strings, spacing, and alignment must return. Relaunch: one custom line appears without duplicates. In a test session, force-end the controller and verify automatic restoration, normally within one second if Explorer is responsive. The helper remains pinned until Explorer exits. A blocked shell UI thread can delay both normal and crash restoration.
6. Compare menu icons to the preceding release in both themes. They now use 16-DIP image canvases centered within the existing 20-DIP slots; visible artwork is approximately 20 physical pixels at 150% scaling. Check cursor colors/default/disable, all three acrylic icons, and sun/moon. Shape, square-slot centering, regular text, lowercase labels, and menus staying open must remain intact.

The optional native XAML check creates a hidden, isolated XAML Island using the production ClockView adapter. It measures the single-line layout, invokes the original test button's handler, checks tooltip identity and accessible name, simulates theme/stock updates, and verifies restoration of unset values and bindings. It does not inject Explorer or prove the real calendar flyout works:

```powershell
cmake -S . -B build-tests -G "Visual Studio 17 2022" -A x64 -DACRYLIC_BUILD_TESTS=ON
cmake --build build-tests --config Release --target ClockLayout --parallel 1
.\build-tests\Release\ClockLayout.exe
```

The clock adapter targets internal modern Windows 11 element names. It leaves unrelated panels and legacy clocks unchanged. A shell update can require a revised adapter; third-party clock/taskbar modifiers should be tested separately. No system regional settings or clock registry values are changed by this feature.

## 10. Transient taskbar layouts and hidden speaker (0.11.0)

1. Install `dist/x64-stable/Install.ps1`, then restart Windows Explorer once to replace the pinned older helper. Run Diagnose.ps1 in that folder. Expect `backend: xaml`, `bridgeState: 2`, and the same controller PID across ordinary taskbar interactions. Record accepted background/clock/volume counts and result values.
2. Repeatedly open/close Start, Search, taskbar previews, the tray overflow, Quick Settings, and the calendar. Move between applications, maximize/restore, and open the app's menu/submenu. Leave each flyout open longer than five seconds. The taskbar and single-line clock should not revert because discovery sees extra or missing child islands. Compare diagnostics and `%LOCALAPPDATA%\AcrylicTaskbar\diagnostics.log`: island changes may be logged as retained; ordinary interactions should not alternate Backend=2 and Backend=3. Save a diagnostic/log excerpt with the time of any remaining flicker.
3. Confirm that only the speaker glyph has gone. Network and battery (when present) remain visible and correctly spaced. Click their group and press Win+A: the normal Quick Settings panel and volume slider must still work. Test mute/unmute, several volume levels, changing audio devices, connecting/disconnecting power, and network transitions. Expect `hiddenVolumeIcons >= 1` and `volumeLastResult: 0`; zero is an unsupported/not-yet-discovered target, not proof that the speaker was removed.
4. Cycle all acrylic modes. Speaker hiding and the single-line clock must remain active even in acrylic off. Test both Windows themes, contrast themes, display/text scaling, multiple monitors, primary-monitor changes, auto-hide, sleep/resume, and a lock/unlock cycle. Verify that no unrelated speaker in a flyout or application was hidden.
5. Exit with acrylic on, then separately with acrylic off. The speaker, stock two-line clock, fill/border, and Windows-configured cursor scheme must return. Relaunch and confirm the requested appearance returns. A crashed controller should restore XAML customizations on the responsive UI-thread liveness timers (up to two seconds for speaker/background, one for clock); cursor crash recovery remains separate.
6. Restart Explorer deliberately. Its taskbar necessarily disappears/reappears; the controller should remain running and attach to the new process. Confirm counts recover and clicking/hover still works. This is distinct from an app-driven reset during ordinary interaction. Windows compositor fallback can still change visible acrylic during battery saver or other policy changes.

Native optional checks: build with `-DACRYLIC_BUILD_TESTS=ON`, then run `TaskbarLifetime.exe` and `VolumeLayout.exe` from the Release directory, serially with `ClockLayout.exe` and `BridgeNotify.exe`. The lifetime fixture uses real child HWNDs and an isolated process, without replacing Explorer. The volume fixture uses real XAML controls with test metadata for internal shell class names; it verifies geometry/restoration and rejects unknown structures. It does not prove the current Explorer template or actual Quick Settings flyout behavior on the user's desktop.


## 11. Clock date/fonts, lowercase am/pm, and cursor alignment (0.12.0)

Install dist/x64-clock-menu directly, then restart Windows Explorer once to load the v8 helper. Keep the supplied PP Playground Light and PP Pangaia Ultralight OTFs in their original extracted Downloads folders. No global font installation is needed.

1. Open the tray menu and `clock`. Confirm `date` is at the left and `on` at the far right in secondary color. Click repeatedly: the date disappears/reappears immediately, the time remains, and the menus stay open. At first launch the default is date on with Segoe; an existing saved choice takes precedence.
2. Open `font`, then select `segoe (system)`, `playground`, and `pangia`. Confirm each font name previews its corresponding typeface and the clock visibly uses the actual supplied typefaces. Other menu labels remain regular Segoe. The selected check must move while all three menus stay open. Switch date while using each font.
3. Confirm am/pm is lowercase with every font and both date states. Check noon, midnight, a minute/day rollover, time-zone change, and resume. Date weekday/month retain English title case.
4. With the font picker open, use Left/Escape to return one level, Right to reopen at the selected choice, and pointer movement to leave the picker for date or another root row. Confirm only the appropriate submenu closes. A second tray press closes the entire branch. Check 100%, 150%, and 200% display scales and both Windows modes.
5. Verify the cursor row's icon is slightly lower (1 DIP optical offset), including default and disabled choices, with its previous size, shape, and horizontal position. Compare it beside the panel and sun/moon glyphs.
6. Click and hover the taskbar clock with each font: Windows calendar and tooltip must work. Inspect placement, width, and clipping on each monitor, including larger Windows text size and high contrast. Exit with a custom font selected: normal two-line Windows clock returns, and its calendar/tooltip still work.
7. Relaunch and sign out/in to check date/font persistence. Exit continues to restore the stock clock; it does not erase your choices. Temporarily make an external font unavailable: a new selection should fail with a notification and keep the previous choice. An already-saved missing font should fall back to readable single-line Segoe, respecting date state, with a clockLastResult error.
8. Run Diagnose.ps1: clockDate/clockFont must match choices. A positive singleLineClocks count means the layout was accepted; it cannot establish font appearance, calendar interaction, or compositor output.

Optional native tests: configure with ACRYLIC_BUILD_TESTS=ON. ClockLayout.exe exercises the real XAML controls and both local font images. ClockFonts.exe takes three arguments: the full Playground OTF path, Pangaia OTF path, and a writable output folder. It validates the actual family names, scaling, premultiplied pixels, time-only width, and missing-file rejection, and writes PPM glyph samples for visual inspection. Custom fonts remain external prerequisites for those checks. DirectWrite's [GetMemoryDC contract](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritebitmaprendertarget-getmemorydc) specifies a 32-bit top-down DIB; the glyph image preserves that orientation.

## 12. Clock size/baseline, font-name previews, and secondary chevron (0.12.1)

Install `dist/x64-clock-sizing/Install.ps1`, then restart Windows Explorer once. Even if upgrading from 0.12.0 with the same mapping format, the pinned helper needs an Explorer restart to load its new rendering code.

1. Compare Segoe, Playground, and Pangaia with date both on and off. Their visible text should have comparable size, with Pangaia slightly enlarged and Playground's small lowercase body substantially enlarged. The clock's baseline and layout height should stay stable when switching fonts. Check long decorative strokes and descenders with different weekdays/months, especially a sample such as `Thu May 28  11:59 pm`. Inspect both themes at 100%, 150%, and 200%, larger Windows text size, multiple monitors, and after a minute rollover. Verify no text overlaps adjacent controls or clips at the taskbar edge; shell clipping or very large accessibility scaling can still limit swashes.
2. Open the font picker in both themes. `segoe (system)` uses Segoe, `playground` the actual Playground Light file, and `pangia` the actual Pangaia Ultralight file. Names stay lowercase with their original light weights. All three rows must be legible, align on the same baseline, fit their pills, and retain visible checks, hover/focus contrast, keyboard navigation, and open-menu selection behavior. Temporarily unavailable files should show a readable Segoe name and retain the existing failed-selection notification behavior.
3. Compare Show hidden icons with nearby taskbar glyphs: only its chevron should appear softer, at 60% opacity. Hover/click it repeatedly; its usual button background, hit area, tooltip, and overflow panel must still work. Check both themes, opening/closing direction changes, high contrast, acrylic off, auto-hide, multiple monitors, sleep/resume, and Explorer restart. Unknown templates must remain usable and unchanged.
4. Exit, and separately stop the controller in a test session. The stock chevron opacity, two-line clock, speaker, taskbar fill/border, and configured cursor scheme must return. For a crashed controller, glyph restoration uses its existing UI-thread timer, normally within two seconds when Explorer is responsive. Relaunch to reapply the requested appearance.

Optional `ChevronLayout.exe` runs the production adapter in a hidden native XAML Island. It substitutes only shell class metadata and checks the exact glyph target, neighboring controls, native width/height, click and tooltip identity, changing glyphs, shell opacity writes, template replacement, latest/unset/binding/destructor restoration, and unknown-structure rejection. It does not establish the current Explorer template or actual overflow-panel behavior. Updated ClockFonts and ClockLayout also check measured font size, native baseline/height, swash padding, and repeated custom-font refresh.


## 13. Refined custom clock size and vertical placement (0.12.2)

Install `dist/x64-clock-alignment/Install.ps1`, then restart Windows Explorer once to load the updated helper.

Compare all three fonts in the taskbar with date on and off. Pangaia is one nominal size smaller than 0.12.1 (13 instead of 14 DIP at a 12-DIP Segoe reference). Both Pangaia and Playground sit 1 DIP lower than before at the default text size; this optical adjustment scales with accessibility text size. Their layout slot height stays unchanged. Segoe retains its native presentation. Compare visible vertical centering in both themes and at 100%, 150%, and 200% display scales, with larger text size and on each monitor. Check Playground's long strokes and descenders for clipping or overlap. The font selector retains its actual typeface previews, with the same smaller Pangaia size correction.

Confirm clock calendar/tooltip interaction, date and font switching without menu dismissal, minute rollover, and normal two-line clock restoration on exit. Existing ClockFonts and ClockLayout assertions now check the smaller Pangaia size and the one-DIP optical offset, alongside their previous font/layout/restoration checks.


## 14. Font pixel clarity, individual placement, and outside clicks (0.12.3)

Install `dist/x64-clock-sharp/Install.ps1`, then restart Windows Explorer once. This loads the updated pinned font renderer as well as the new controller menu behavior.

1. Compare the actual taskbar at native monitor resolution in both themes, at 100%, 125%, 150%, 175%, and 200%. Both custom fonts still come from PPPlayground-Light.otf and PPPangaia-Ultralight.otf in the supplied external folders. Their light/ultralight weights are preserved. Pangaia keeps its smaller size and moves up one nominal step from 0.12.2; Playground moves down one nominal step. Check date on/off, long strokes/descenders, minute rollover, and larger Windows text size. The final bitmap origin is rounded to the nearest physical screen pixel, so fractional scales can introduce up to half a pixel of baseline rounding.
2. Inspect the native-resolution clock, rather than a resized screenshot. Its source raster now spans exactly the same number of destination screen pixels and its absolute image origin is snapped after layout. Look for any remaining softness, clipping, or jitter on thin strokes, after moving taskbar/primary monitor or changing scale. The transparent glyph rendering still uses grayscale antialiasing and the supplied thin font weights; it does not apply fake bold or promise the appearance of regular Segoe. Verify calendar clicks, tooltip, and exit restoration.
3. With only the root menu open, click outside on another application, empty desktop, an empty taskbar area, the clock, and another tray icon. The menu must close, and the clicked target must receive its normal action. Repeat with cursor and three-level clock/font menus open. Test left/right/middle/X buttons where appropriate. An inside press in any visible menu level must keep the settings flow open; the normal child hover rules still apply. Press the utility's own tray icon twice: it must open then close, without immediately reopening.
4. Check repeated menu open/dismiss cycles, Escape, exit, focus changes, both Windows themes, and supported display scales. The outside mouse observer is removed immediately when the menu closes. It only examines pointer-down coordinates while open and always passes input to the next hook/target; it stores no input history. Existing activation-based dismissal remains available if the observer cannot be installed.

Updated ClockFonts checks exact source/destination pixel spans at all five scales. ClockLayout checks actual XAML image dimensions/origin, a fractional ancestor transform, font-specific optical placement, switching/date behavior and restoration. The popup harness exercises the same outside-press observer on its UI thread without moving the user's pointer, alongside native mouse-hook installation and cleanup. Actual cross-application click delivery and the real Explorer clock/tooltip remain desktop checks.

## 15. Flower tray icon and Playground position (0.12.4)

Install `dist/x64-flower/Install.ps1`, then restart Windows Explorer once to replace the pinned clock renderer.

1. Confirm Element 41's flower appears in the notification area or overflow, with a transparent center and the original shape/proportions. In Windows light mode it is black; in dark mode it is white. There must be no green. Check 100%, 125%, 150%, 175%, and 200% display scale, both themes and contrast themes. Move the taskbar between monitors where supported, and restart Explorer to confirm re-registration.
2. Click the flower twice to open and close the menu. Right-click and keyboard activation continue to work. Cycle all acrylic modes: only the menu row and tooltip describe the selected mode; the flower stays a consistent entry point. Existing cursor/date/font/theme selections and outside-click dismissal remain.
3. Compare Playground against 0.12.3: it is one nominal step higher, back to the 0.12.2 placement target of 1 DIP below Segoe at the default text size. Pangaia retains its 0.12.3 baseline and size. Exact physical placement rounds to a whole pixel. Check time/date on/off, text scaling, calendar click, tooltip and exit restoration.

Native ClockLayout checks cover the updated target together with exact pixel dimensions, whole-pixel origins, fractional ancestors, native slot height and restoration. Compiled icon resource checks load every size; generated alpha masks are checked for transparent centers and matching light/dark geometry. Final Explorer appearance, theme transitions and physical interaction remain desktop checks.

## 16. Larger Element 48 tray artwork (0.12.5)

Install `dist/x64-element48/Install.ps1`. From 0.12.4 an Explorer restart is unnecessary: the pinned helper has identical bytes. From an older release restart Explorer once for the previous clock updates.

1. Find the new four-lobed Element 48 shape in the notification area/overflow. It should occupy more of its square than Element 41, remain centered, retain its transparent center, and have no green. Compare legibility at 100%, 125%, 150%, 175% and 200% display scales in both Windows themes. The visible artwork is black in light mode and white in dark mode; changing acrylic mode retains the shape.
2. Verify click/right-click/keyboard activation opens the menu, a second tray activation closes it, and the current-mode tooltip still updates. Restart Explorer separately to verify tray re-registration. Exit removes the icon as before. The menu icons, settings, Playground position, other clock fonts and cursor appearance are unchanged.

The matching vector asset from the same supplied pack is used instead of downscaling the PNG. Native resource-loading checks and pixel comparisons cover all eight embedded sizes, matching theme alpha, transparent centers and monochrome ink. Final taskbar legibility and physical activation remain desktop checks.

## 17. Supplied cursor SVG colors and secondary acrylic mode (0.12.6)

Install `dist/x64-cursor-svg/Install.ps1`. Upgrading from 0.12.4 or later requires only the controller restart performed by the installer. From earlier versions restart Explorer once for the previous clock updates.

1. The acrylic row must read `acrylic` at left and `system`, `pure`, or `off` at the far right, in the same secondary color/alignment as date on/off. Its panel icon still reflects the mode. Cycle with clicks and Enter/Space: system → pure → off → system. The same menu and any open submenu stay open, the secondary text updates, and the row's accessible name includes its state. Compare all three states and both Windows themes at 100%, 150%, and 200% display scaling. Check regular font weight, alignment and spacing without label/status overlap.
2. The cursor row must use the supplied SVG silhouette and stroke, centered in its existing square slot. Select all nine cursor colors without closing the menu. Both the inside and outline should match that actual cursor: light white/black, dark charcoal/white, blueberry periwinkle/dark purple, and the other variants' corresponding tinted borders. Explicit colors keep the same pair when the Windows/menu theme changes; default follows the light/dark cursor choice. Disable uses the same SVG shape with Microsoft's colors. No theme-dependent stroke thickness is added.
3. Check cursor selection while the acrylic row is cycled; then open clock/date/font, use keyboard and delayed submenu hover, click outside and toggle the tray icon. Existing live-settings and dismissal behavior remain. The Element 48 tray glyph and clock font placements are unchanged.

CursorIcons checks all nine actual source palettes, the shared vector alpha/centering, premultiplied pixels, stock and absent/invalid-file fallback at seven sizes. CursorLogic simulates system writes while checking real cursor-file loading. The native popup harness checks row accessibility, live cycling and retained menu behavior. Visual appearance on the real desktop remains a user check.

## Material and menu regression checks

Build with `-DACRYLIC_BUILD_TESTS=ON`, then run `AcrylicMaterial.exe`, `ClockLayout.exe`, `BridgeNotify.exe`, and `UpdateLogic.exe` from the Release output. AcrylicMaterial exercises real Windows.UI.Xaml AcrylicBrush objects through theme and pure/system transitions. It checks explicit luminosity and models achromatic black/white backdrop contrast, legacy overlay contrast, and solid fallback. This model does not measure GPU output. BridgeNotify also checks detection of an older pinned v8 mapping, compatibility of v9, and clearing the reload requirement after the older mapping disappears.

Follow steps 13–16 in [INSTALLATION_TESTS.md](INSTALLATION_TESTS.md) for actual taskbar brightness, live menu blur, fallback, and upgrade verification. Record the compositor result separately from accepted properties and diagnostic counts. Native offscreen menu rendering verifies transparent background coverage and premultiplied foreground pixels; it does not capture DWM's final material.

Run `MenuPresentation.exe <output-folder>` for production popup drawing at 96/144/192 DPI in both themes: system-first/date ordering, calendar toggles, regular Windows 11 system typography, version/update states, spinner frames, locked colors action/live import, cursor keyboard behavior, and premultiplied foreground. Captures preview the foreground over a solid theme color.

Add `--compositor` on an interactive Windows desktop to briefly show only the fixture's own background/menu windows and capture the menu bounds. The test repaints its background red and blue and requires corresponding actual screen-pixel changes in both themes. It takes no screenshots outside the test menu and changes no shell, registry, cursor, location or theme settings. Inspect these captures for sharp regular text and complete icons as well as material transparency.

Run `SetupLockLogic.exe`, `SetupRecovery.exe`, `SolarLogic.exe` and `ThemeLogic.exe` for lock ownership/completion-dialog behavior, registration snapshot rollback, solar boundaries/polar/date-line cases and simulated theme write/rollback failures. UpdateLogic also checks exact SHA-256/size, corrupted/truncated files, cancellation before handoff and strict redirect origin rules. To exercise the production HTTP downloader without executing an installer, save GitHub's latest-release JSON to a file and run `UpdateLogic.exe --download-live <json-file>`; use a fresh output directory. The test creates `native-downloaded-setup.exe` beside the JSON and refuses to overwrite it. See normal-desktop installation tests for the later-release replacement and real consent/provider cases that these isolated checks cannot establish.


For 0.16.0 run `NetworkLayout.exe` for the production connected-network adapter on a native XAML fixture: supported connected glyphs, immediate offline/warning/unknown restoration, foreground binding, unchanged measured width, click/tooltip preservation, overlay recovery, latest opacity/binding restoration, and rejection of unsupported slot layouts. The fixture does not change the system network or inject into Explorer. MenuPresentation additionally resolves the actual GDI-selected Segoe UI Variable Text face, checks 14-DIP regular typography, validates bottom/right placement at five DPIs, and exercises the production entrance curve/position/end cleanup. It does not certify the private shell's animation parameters or physical smoothness on every display.


For 0.16.1, NetworkLayout models the full IconView/ContentPresenter/ContentGrid/TextIconContent/ContainerGrid/Base/InnerTextBlock template instead of the incomplete v0.16.0 mock. It exercises every MobWifi/SysWifi strength and TVMonitor/Ethernet glyph, both offline sets, warnings/airplane/connecting/unknown states, reordered slots, and initially offline selection. MenuPresentation resolves Segoe UI (not Variable Text) at regular weight 400 and heights 14/21/28 for 96/144/192 DPI. AcrylicMaterial checks the reduced brightness layer leaves at least 25% background contribution in its contrast model, alongside black/white contrast and real brush properties. BridgeNotify rejects pinned v10 mappings for v11. None of these fixtures modifies system connectivity or proves the private Explorer hierarchy matched on every Windows build.


For 0.16.2, NetworkLayout runs the full production overlay suite twice: direct ContainerGrid > ContentGrid, and optional ContentPresenter > ContentGrid. The direct fixture failed against the previous production selector before applying the fix. This corrects the mandatory-wrapper assumption in the 0.16.1 test. MenuPresentation compares family, size and weight to Windows' NONCLIENTMETRICS. Injected alternate metrics and a failed-query fallback establish that all three values come from Windows. Production drawing must use DirectWrite without GDI fallbacks, with premultiplied native alpha, opaque glyph stems, smooth edge coverage, transparent empty pixels and ink-based icon alignment in both themes at 96/144/192 DPI. Width checks use DirectWrite too. The compositor fixture keeps its controlled background visible without taking focus and checks that another app has not covered it before sampling transparency. BridgeNotify rejects pinned v11 mappings for v12.


## 0.16.3 native checks

MenuPresentation validates the installed Segoe UI Variable typographic family, regular weight, 14-DIP size, 20-DIP line height and automatic optical axis at 96/120/144/168/192 DPI. Both themes and all menu states use native DirectWrite; the forced compatibility test uses Segoe UI. Premultiplied alpha, glyph stems/edges, transparent empty pixels, text measurement, icon alignment, live compositor backdrops and no production GDI fallbacks are checked. VolumeLayout retains native network neighbors; BridgeNotify rejects helper v12 for v13.

StartupLogic parses the production definition through Windows Task Scheduler without registering a task, checks account/logon/normal privilege/no delay/battery/network/idle/runtime settings and exact XML-escaped paths, tests obsolete-owner removal and task-XML rollback with in-memory storage, and checks bounded fast shell retry policy. SetupRecovery tests Run/uninstall snapshots without real registry writes. Task Scheduler access may require running the read-only definition test outside a restricted build sandbox. Real task registration, cold boot latency, AC/battery startup, duplicate startup, failed update and uninstall are desktop checks in INSTALLATION_TESTS step 25.

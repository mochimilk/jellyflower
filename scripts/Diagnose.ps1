[CmdletBinding()]
param([string]$OutputPath = (Join-Path $PSScriptRoot 'diagnostic.json'))
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'jellyflower.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run this script from the built distribution folder.' }
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
if ($OutputPath.Contains('"')) { throw 'The output path cannot contain a quotation mark.' }
$diagnostic = Start-Process -FilePath $executable -ArgumentList @('--diagnose', ('"' + $OutputPath + '"')) -WindowStyle Hidden -Wait -PassThru
if ($diagnostic.ExitCode -ne 0) { throw "Diagnostics failed (Windows error $($diagnostic.ExitCode))." }
Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json | Format-List
Write-Host "Saved $OutputPath"
Write-Host 'Applied backgrounds means the API/brush request succeeded. Inspect the taskbar visually to verify blur.'
Write-Host 'singleLineClocks counts accepted clock layouts. If zero, the helper has not found a supported clock; check bridgeState and clockLastResult.'
Write-Host 'hiddenVolumeIcons counts hidden speaker glyphs. Battery and the Quick Settings click target remain native.'
Write-Host 'bridgeRetained means a live Explorer connection is being kept across transient layout changes. taskbarXamlIslands is a discovery count, not a blur measurement.'

Write-Host 'clockDate reports the saved date toggle. The single-line clock uses the native Windows system font.'

Write-Host 'startSurfaces/startAttached count accepted Start background layers/connection. Inspect Start visually; startLastResult reports an HRESULT.'

Write-Host 'trayOnlyJellyflower reports the saved tray choice. hiddenTrayParts counts collapsed tray parts; trayJellyflowerIcons counts taskbars where the flower was found (the chevron stays visible where it is 0). trayLastResult reports an HRESULT.'

Write-Host 'keyboardLayout reports the saved touch keyboard choice. keyboardAttached is the touch keyboard host connection (attached only once symmetric is chosen); keyboardSymmetricGrids counts keyboard pages showing the symmetric layout (0 while the keyboard is hidden or on a page it leaves stock). keyboardLastResult reports an HRESULT.'

Write-Host 'keyboardField is what the touch keyboard''s return key follows: empty (dimmed), text (accent), unknown (Windows'' look; passwords and apps that report no text), or off when the symmetric layout is off.'

Write-Host 'compactDisplays counts displays narrower than Start that have window margins (0 on wider screens). statusBar reports the saved status bar choice; the margins stay when it is off. compactLastResult reports an HRESULT.'

Write-Host 'floatingDock reports the saved dock choice. dockTaskbars counts taskbars drawn as a floating pill (0 while no app button is visible), clickThroughTaskbars those whose empty parts pass clicks through. dockLastResult reports an HRESULT: ERROR_ALREADY_EXISTS (0x800700B7) means another program shapes the taskbar and click-through was left off, TYPE_E_AMBIGUOUSNAME (0x8002802C) that two identical taskbars could not be told apart.'

Write-Host 'taskbarHeight is the taller bar in DIPs (64 while floating is on, 0 for the stock bar). taskbarHeightResult reports an HRESULT: ERROR_NOT_FOUND (0x80070490) or an HTTP error if Taskbar.dll''s public symbols could not be found or downloaded, ERROR_REVISION_MISMATCH (0x8007049A) if the slot no longer holds Windows'' method. Either way the dock keeps floating at 48 DIPs.'

Write-Host 'menuGlassResult reports an HRESULT: the last failure to draw a menu''s fluted glass, 0 once a menu has it again. A menu without it falls back to Windows'' acrylic, as it does while energy saver is on.'

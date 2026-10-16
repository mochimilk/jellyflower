[CmdletBinding()]
param([string]$CursorPackPath = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads\cursor-concept-3-hdpi'))
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'jellyflower.exe'
$bridge = Join-Path $PSScriptRoot 'jellyflower-bridge-v17.dll'
$startBridge = Join-Path $PSScriptRoot 'jellyflower-start-v19.dll'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $bridge) -or -not (Test-Path -LiteralPath $startBridge)) {
    throw 'Run Build.ps1 first, then use Install.ps1 from dist\x64 or dist\ARM64. Keep the executable and helper DLLs together.'
}
$stop = Start-Process -FilePath $executable -ArgumentList '--stop' -WindowStyle Hidden -Wait -PassThru
if ($stop.ExitCode -ne 0) { throw "Could not stop the existing instance (Windows error $($stop.ExitCode))." }
if (Test-Path -LiteralPath $CursorPackPath -PathType Container) {
    $pack = (Resolve-Path -LiteralPath $CursorPackPath).Path
    $configuration = Start-Process -FilePath $executable -ArgumentList @('--cursor-pack', ('"' + $pack + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($configuration.ExitCode -ne 0) { Write-Warning "Cursor pack configuration failed (Windows error $($configuration.ExitCode)). Taskbar installation will continue." }
}
$registration = Start-Process -FilePath $executable -ArgumentList '--install' -WindowStyle Hidden -Wait -PassThru
if ($registration.ExitCode -ne 0) { throw "Startup registration failed (Windows error $($registration.ExitCode))." }
Start-Process -FilePath $executable -WindowStyle Hidden | Out-Null
Write-Host 'Login startup enabled for your Windows account. jellyflower launched.'
Write-Host 'Look under Show hidden icons for the jellyflower icon. Click or right-click to open the menu; click the acrylic row to cycle system, pure, and off.'
Write-Host 'The modern Windows 11 clock uses one line: Mon Jun 22  9:41 am. Its calendar button and tooltip stay with Windows.'
Write-Host 'The date row toggles the date. The clock uses the Windows system font.'
Write-Host 'Show hidden icons uses a softer 60% chevron. Startup uses a no-delay, normal-user logon task with a Run fallback.'
Write-Host 'The speaker glyph is hidden on supported Windows 11 layouts. Network, battery, and Quick Settings remain; Win+A also opens the volume slider.'
Write-Host 'Setting changes keep the menu open. General > exit restores Start styling, the taskbar, normal clock, speaker glyph, and Windows cursor scheme; your Windows mode stays saved.'
Write-Host 'Open general for the version/update row and exit. Upgrading from before 0.16.3 may need one sign-out/sign-in to load taskbar helper v13.'
Write-Host 'Quit TranslucentTB before using jellyflower. Run Diagnose.ps1 to inspect status.'

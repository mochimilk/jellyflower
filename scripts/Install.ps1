[CmdletBinding()]
param([string]$CursorPackPath = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads\cursor-concept-3-hdpi'))
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'jellyflower.exe'
$bridge = Join-Path $PSScriptRoot 'jellyflower-bridge.dll'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $bridge)) {
    throw 'Run Build.ps1 first, then use Install.ps1 from dist\x64 or dist\ARM64. Keep the executable and bridge DLL together.'
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
Write-Host 'clock > date toggles the date. clock > font selects segoe (system), playground, or pangia; keep the supplied OTF files in Downloads.'
Write-Host 'Font names preview their typefaces. Clock fonts use balanced size and placement; Show hidden icons uses a softer 60% chevron.'
Write-Host 'The speaker glyph is hidden on supported Windows 11 layouts. Network, battery, and Quick Settings remain; Win+A also opens the volume slider.'
Write-Host 'Setting changes keep the menu open. Exit restores the taskbar, normal clock, speaker glyph, and Windows cursor scheme; your Windows mode stays saved.'
Write-Host 'If upgrading from a version before 0.12.4, restart Windows Explorer once in Task Manager to load the updated clock helper. The taskbar briefly disappears and returns.'
Write-Host 'Quit TranslucentTB before using jellyflower. Run Diagnose.ps1 to inspect status.'

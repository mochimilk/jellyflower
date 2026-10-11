[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'jellyflower.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run this script from the built distribution folder.' }
$removal = Start-Process -FilePath $executable -ArgumentList '--uninstall' -WindowStyle Hidden -Wait -PassThru
if ($removal.ExitCode -ne 0) {
    throw "Uninstall failed (Windows error $($removal.ExitCode)). Error 183 means another copy owns the startup registration; use that copy's Uninstall.ps1."
}
Write-Host 'Login startup removed and utility stopped. The taskbar restores within about two seconds on the modern backend.'
Write-Host 'The Windows-configured cursor scheme is restored. Saved cursor preferences remain under HKCU\Software\AcrylicTaskbar.'
Write-Host 'Windows keeps the modern bridge DLL loaded in Explorer until you sign out or restart Explorer. Delete the distribution after that.'

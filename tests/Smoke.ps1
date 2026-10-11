# Launches the actual utility and therefore changes the taskbar appearance.
# Does not register login startup or toggle battery saver.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ExecutablePath,
    [string]$WindowClass = 'AcrylicTaskbar.Controller.v1',
    [string]$TrayInputProbe
)
$ErrorActionPreference = 'Stop'
$ExecutablePath = (Resolve-Path -LiteralPath $ExecutablePath).Path
if (Get-Process -Name AcrylicTaskbar,jellyflower -ErrorAction SilentlyContinue) {
    throw 'Stop the existing AcrylicTaskbar instance before running this test.'
}
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ('AcrylicTaskbar-' + [guid]::NewGuid().ToString() + '.json')
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Read-Diagnostic {
    $read = Start-Process -FilePath $ExecutablePath -ArgumentList @('--diagnose', ('"' + $temporary + '"')) -WindowStyle Hidden -Wait -PassThru
    Assert-True ($read.ExitCode -eq 0) "Diagnostic command failed: $($read.ExitCode)"
    Get-Content -LiteralPath $temporary -Raw | ConvertFrom-Json
}
$first = $null
if (-not ('AcrylicTraySmoke.Native' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
namespace AcrylicTraySmoke {
    public static class Native {
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindow(string className, string title);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        public static extern bool IsWindowVisible(IntPtr window);
    }
}
'@
}
function Invoke-Menu([string]$Action) {
    if ($TrayInputProbe) {
        $activation = Start-Process -FilePath $TrayInputProbe -ArgumentList @(('"' + $WindowClass + '"'), $Action) -WindowStyle Hidden -Wait -PassThru
        Assert-True ($activation.ExitCode -eq 0) "Native tray callback failed (Windows error $($activation.ExitCode))."
        return
    }
    $window = [AcrylicTraySmoke.Native]::FindWindow($WindowClass, $null)
    $trayEvent = if ($Action -eq 'menu-click-escape') { 0x400 } elseif ($Action -eq 'menu-key-escape') { 0x401 } else { 0x7b }
    # Deliver the shell's v4 callback to the real native controller. This tests
    # mouse/keyboard routing and state, without claiming a visible tray click.
    $sent = [AcrylicTraySmoke.Native]::PostMessage($window, 0x802b, [UIntPtr]::Zero, [IntPtr](0x10000 -bor $trayEvent))
    Assert-True $sent "Tray callback failed (Windows error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
    $popup = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        $popup = [AcrylicTraySmoke.Native]::FindWindow('AcrylicTaskbar.TrayMenu.v1', 'jellyflower')
        if ([AcrylicTraySmoke.Native]::IsWindowVisible($popup)) { break }
        Start-Sleep -Milliseconds 20
    }
    Assert-True ([AcrylicTraySmoke.Native]::IsWindowVisible($popup)) 'Tray activation did not open the menu.'
    if ($Action -eq 'menu-toggle') {
        [void][AcrylicTraySmoke.Native]::PostMessage($popup, 0x111, [UIntPtr]1, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        Assert-True ([AcrylicTraySmoke.Native]::IsWindowVisible($popup)) 'Changing acrylic closed the menu.'
    }
    [void][AcrylicTraySmoke.Native]::PostMessage($popup, 0x100, [UIntPtr]0x1b, [IntPtr]::Zero)
    for ($attempt = 0; $attempt -lt 100 -and [AcrylicTraySmoke.Native]::IsWindowVisible($popup); $attempt++) { Start-Sleep -Milliseconds 20 }
    Assert-True (-not [AcrylicTraySmoke.Native]::IsWindowVisible($popup)) 'Menu did not dismiss after the selected action.'
}
try {
    $initial = Read-Diagnostic
    Assert-True (-not $initial.running) 'An instance already owns the controller window.'
    $first = Start-Process -FilePath $ExecutablePath -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 2
    $before = Read-Diagnostic
    Assert-True ($before.running -and $before.responsive -and $before.pid -eq $first.Id) 'The controller did not become responsive.'
    Assert-True ($before.acrylicEnabled) 'Acrylic was not enabled on launch.'
    Invoke-Menu 'menu-click-escape'
    Invoke-Menu 'menu-key-escape'
    Assert-True ((Read-Diagnostic).acrylicEnabled) 'Opening the menu changed acrylic.'
    Invoke-Menu 'menu-toggle'
    $pure = Read-Diagnostic
    Assert-True ($pure.acrylicEnabled -and $pure.acrylicMode -eq 'pure') 'First click did not select pure acrylic.'
    Invoke-Menu 'menu-toggle'
    Start-Sleep -Seconds 6 # Cross the periodic recovery tick while switched off.
    $off = Read-Diagnostic
    Assert-True (-not $off.acrylicEnabled -and $off.pid -eq $first.Id -and $off.responsive) 'Menu selection did not turn acrylic off while keeping the controller running.'
    Invoke-Menu 'menu-click-escape'
    Invoke-Menu 'menu-key-escape'
    Assert-True (-not (Read-Diagnostic).acrylicEnabled) 'Opening the menu changed acrylic while off.'
    Invoke-Menu 'menu-toggle'
    Start-Sleep -Milliseconds 200
    $on = Read-Diagnostic
    Assert-True ($on.acrylicEnabled -and $on.pid -eq $first.Id -and $on.responsive) 'Menu selection did not turn acrylic back on.'
    $second = Start-Process -FilePath $ExecutablePath -WindowStyle Hidden -PassThru
    Assert-True ($second.WaitForExit(5000)) 'Duplicate instance did not exit promptly.'
    Assert-True ($second.ExitCode -eq 0) 'Duplicate launch failed.'
    $after = Read-Diagnostic
    Assert-True ($after.pid -eq $first.Id -and $after.responsive) 'Duplicate launch displaced the first instance.'
    $stop = Start-Process -FilePath $ExecutablePath -ArgumentList '--stop' -WindowStyle Hidden -Wait -PassThru
    Assert-True ($stop.ExitCode -eq 0 -and $first.WaitForExit(5000)) 'Graceful stop failed.'
    $stopped = Read-Diagnostic
    Assert-True (-not $stopped.running) 'Controller window remained after stop.'
    # A pinned modern bridge must allow attaching a fresh controller without
    # restarting Explorer. This exercises shared-state ownership and epochs.
    $first = Start-Process -FilePath $ExecutablePath -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    $restarted = Read-Diagnostic
    Assert-True ($restarted.running -and $restarted.responsive -and $restarted.pid -eq $first.Id) 'Restart did not acquire the controller.'
    Assert-True ($restarted.acrylicEnabled) 'Restart did not reset acrylic to on.'
    Write-Host 'PASS: startup on, mouse/keyboard open menu without toggling, menu toggle, single instance, diagnostics, graceful stop, and restart.'
    Write-Host "Observed backend: $($restarted.backend). Rendering and battery saver require the manual tests."
} finally {
    if ($first -and -not $first.HasExited) {
        $cleanup = Start-Process -FilePath $ExecutablePath -ArgumentList '--stop' -WindowStyle Hidden -Wait -PassThru
        if ($cleanup.ExitCode -ne 0) { Write-Warning 'Could not stop the test instance; use Task Manager.' }
    }
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}

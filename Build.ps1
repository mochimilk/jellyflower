[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $cmakePath = $candidate }
        }
    }
}
if (-not $cmakePath) {
    throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++, C++ CMake tools, and Windows SDK 10.0.20348 or newer. No C++ compiler/SDK is bundled in this project.'
}
$buildPath = Join-Path $projectRoot "build-$Architecture"
$distribution = Join-Path $projectRoot "dist\$Architecture"
& $cmakePath -S $projectRoot -B $buildPath -G $Generator -A $Architecture
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
& $cmakePath --build $buildPath --config $Configuration --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
& $cmakePath --install $buildPath --config $Configuration --prefix $distribution
if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)." }
Write-Host "Built $distribution"
Write-Host "Double-click $distribution\jellyflower-setup-$Architecture.exe to install jellyflower."

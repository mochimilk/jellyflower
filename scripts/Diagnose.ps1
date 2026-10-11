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
Write-Host 'replacedNetworkIcons counts accepted connected-network replacements. Offline/warning icons and unknown layouts remain native; networkLastResult records helper errors. networkGlyphCode is the last detected numeric glyph (zero means no matched network template), never a network name. Interpret helper fields only when responsive is True.'
Write-Host 'bridgeRetained means a live Explorer connection is being kept across transient layout changes. taskbarXamlIslands is a discovery count, not a blur measurement.'

Write-Host 'clockDate reports the saved date toggle. The single-line clock uses the native Windows system font.'

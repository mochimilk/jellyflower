[CmdletBinding()]
param([string]$AssetsFolder = (Join-Path $PSScriptRoot 'dist\x64'))
$ErrorActionPreference = 'Stop'
$repository = 'mochimilk/jellyflower'
$version = ([regex]::Match((Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw), 'project\(jellyflower VERSION (\d+\.\d+\.\d+)')).Groups[1].Value
if (-not $version) { throw 'Cannot read the project version.' }
$installer = Join-Path $AssetsFolder 'jellyflower-setup-x64.exe'
if (-not (Test-Path -LiteralPath $installer)) { throw 'Build the x64 installer first with Build.ps1.' }
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)." }
}
function Read-Native([string]$Program, [string[]]$Arguments) {
    # Windows PowerShell 5 treats native stderr as terminating with Stop,
    # including the expected 404 for a repository/release not created yet.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & $Program @Arguments 2>$null
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Text = ($lines -join [Environment]::NewLine) }
    } finally { $ErrorActionPreference = $previousPreference }
}
$profileRead = Read-Native 'gh' @('api','user')
if ($profileRead.ExitCode -ne 0) { throw 'Run gh auth login -h github.com -p https -w, then run this script again.' }
$profile = $profileRead.Text | ConvertFrom-Json
if ($profile.login -ne 'mochimilk') { throw "Sign in as mochimilk first; currently signed in as $($profile.login)." }
# GitHub requires this scope for pushing the included Actions workflow.
$headers = & gh api -i user
if ($LASTEXITCODE -ne 0) { throw 'Could not check GitHub permissions.' }
$scopes = $headers | Where-Object { $_ -match '^X-Oauth-Scopes:' }
if ($scopes -and $scopes -notmatch '\bworkflow\b') { Invoke-Checked 'gh' @('auth','refresh','-h','github.com','-s','workflow') }
Push-Location -LiteralPath $PSScriptRoot
try {
    $repoInfo = Read-Native 'gh' @('api',"repos/$repository")
    if ($repoInfo.ExitCode -ne 0) {
        # Auth was checked above. Do not change or overwrite an unrelated repo.
        Invoke-Checked 'gh' @('repo','create',$repository,'--public','--description','A small native Windows taskbar utility: acrylic, cursors, and a single-line clock.')
    } else {
        $repo = $repoInfo.Text | ConvertFrom-Json
        if ($repo.private) { throw 'jellyflower already exists as a private repository. Public downloads need a public repository; no visibility was changed.' }
    }
    if (-not (Test-Path -LiteralPath '.git')) { Invoke-Checked 'git' @('init','-b','main') }
    Invoke-Checked 'git' @('config','user.name',$(if ($profile.name) { $profile.name } else { $profile.login }))
    Invoke-Checked 'git' @('config','user.email',"$($profile.id)+$($profile.login)@users.noreply.github.com")
    $origin = Read-Native 'git' @('remote','get-url','origin')
    if ($origin.ExitCode -ne 0) { Invoke-Checked 'git' @('remote','add','origin',"https://github.com/$repository.git") }
    elseif ($origin.Text -notin @("https://github.com/$repository.git","https://github.com/$repository")) { throw "Unexpected origin: $($origin.Text)" }
    Invoke-Checked 'git' @('add','--all')
    & git diff --cached --quiet
    if ($LASTEXITCODE -eq 1) { Invoke-Checked 'git' @('commit','-m',"Release jellyflower $version") }
    elseif ($LASTEXITCODE -ne 0) { throw 'Could not inspect staged files.' }
    Invoke-Checked 'gh' @('auth','setup-git','-h','github.com')
    Invoke-Checked 'git' @('push','-u','origin','HEAD:main')
    # The local x64 installer is ready immediately. Tags publish both architectures
    # through Actions for future releases; this command does not push a tag.
    $existingRelease = Read-Native 'gh' @('release','view',"v$version",'--repo',$repository,'--json','isDraft')
    if ($existingRelease.ExitCode -eq 0 -and -not ($existingRelease.Text | ConvertFrom-Json).isDraft) {
        throw "v$version is already published. Bump the version before publishing another build."
    }
    if ($existingRelease.ExitCode -ne 0) {
        Invoke-Checked 'gh' @('release','create',"v$version",'--repo',$repository,'--target','main','--title',"jellyflower $version",'--notes-file',(Join-Path $PSScriptRoot 'docs\RELEASE.md'),'--draft')
    }
    $assets = @($installer)
    foreach ($name in @('jellyflower-x64.zip','jellyflower-source.zip','SHA256SUMS.txt')) {
        $path = Join-Path $AssetsFolder $name
        if (Test-Path -LiteralPath $path) { $assets += $path }
    }
    Invoke-Checked 'gh' (@('release','upload',"v$version",'--repo',$repository,'--clobber') + $assets)
    Invoke-Checked 'gh' @('release','edit',"v$version",'--repo',$repository,'--draft=false','--latest')
    Write-Host "Published https://github.com/$repository/releases/tag/v$version"
} finally { Pop-Location }

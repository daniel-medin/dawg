param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$helperPath = Join-Path $scriptRoot 'Invoke-DawgDev.ps1'

$excludedPathFragments = @(
    '\.git\',
    '\.vs\',
    '\.tools\',
    '\build\',
    '\b\',
    '\rel\',
    '\rel2\',
    '\out\'
)

$watchedExtensions = @(
    '.c',
    '.cc',
    '.cmake',
    '.cmd',
    '.cpp',
    '.h',
    '.hpp',
    '.ico',
    '.jpeg',
    '.jpg',
    '.json',
    '.png',
    '.ps1',
    '.qrc',
    '.qss',
    '.svg',
    '.txt',
    '.ui'
)

function Write-Step {
    param([string]$Message)

    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Should-WatchPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    foreach ($fragment in $excludedPathFragments) {
        if ($fullPath -like "*$fragment*") {
            return $false
        }
    }

    $name = [System.IO.Path]::GetFileName($fullPath)
    if ($name -in @('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json')) {
        return $true
    }

    $extension = [System.IO.Path]::GetExtension($fullPath)
    return $watchedExtensions -contains $extension
}

function Get-WatchedFiles {
    Get-ChildItem -Path $repoRoot -Recurse -File | Where-Object {
        Should-WatchPath -Path $_.FullName
    }
}

function Get-WatchSnapshot {
    $snapshot = @{}
    foreach ($file in Get-WatchedFiles) {
        $snapshot[$file.FullName] = $file.LastWriteTimeUtc.Ticks
    }

    return $snapshot
}

function Get-ChangedPaths {
    param(
        [hashtable]$Previous,
        [hashtable]$Current
    )

    $changedPaths = [System.Collections.Generic.List[string]]::new()

    foreach ($path in $Current.Keys) {
        if (-not $Previous.ContainsKey($path) -or $Previous[$path] -ne $Current[$path]) {
            $changedPaths.Add($path)
        }
    }

    foreach ($path in $Previous.Keys) {
        if (-not $Current.ContainsKey($path)) {
            $changedPaths.Add($path)
        }
    }

    return $changedPaths
}

function Invoke-Rebuild {
    param([string]$Reason)

    Write-Step "Rebuilding because $Reason"
    & $helperPath -Configuration $Configuration -KillRunning -Launch
}

Write-Step "Initial build and launch"
& $helperPath -Configuration $Configuration -KillRunning -Launch

$previousSnapshot = Get-WatchSnapshot

Write-Step "Watching for changes"
Write-Host "Save a file in the repo and DAWG will rebuild, restart, and relaunch."
Write-Host "Press Ctrl+C to stop."

while ($true) {
    Start-Sleep -Seconds 1

    $currentSnapshot = Get-WatchSnapshot
    $changedPaths = @(Get-ChangedPaths -Previous $previousSnapshot -Current $currentSnapshot)
    if ($changedPaths.Count -eq 0) {
        continue
    }

    Start-Sleep -Milliseconds 500
    $currentSnapshot = Get-WatchSnapshot
    $changedPaths = @(Get-ChangedPaths -Previous $previousSnapshot -Current $currentSnapshot)
    if ($changedPaths.Count -eq 0) {
        continue
    }

    $reason = "you changed $([System.IO.Path]::GetFileName($changedPaths[-1]))"

    try {
        Invoke-Rebuild -Reason $reason
        $previousSnapshot = Get-WatchSnapshot
    }
    catch {
        $previousSnapshot = Get-WatchSnapshot
        Write-Host ""
        Write-Host "Build failed. Watching for the next change..." -ForegroundColor Yellow
        Write-Host $_.Exception.Message -ForegroundColor Yellow
    }
}

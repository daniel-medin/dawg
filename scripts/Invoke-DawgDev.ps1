param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Launch,
    [switch]$KillRunning
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)

    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Ensure-Command {
    param(
        [string]$Name,
        [string]$InstallHint
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found. $InstallHint"
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command,
        [Parameter(Mandatory = $true)]
        [string]$FailureMessage
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Stop-DawgProcess {
    $processes = @(Get-Process -Name dawg -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) {
        return
    }

    Write-Step "Stopping running DAWG"
    $processes | Stop-Process -Force
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$buildRoot = Join-Path $repoRoot 'build\windows-msvc-current'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$exePath = Join-Path $buildRoot "$Configuration\dawg.exe"

Write-Step "Checking prerequisites"
Ensure-Command -Name cmake -InstallHint "Install CMake and re-open the terminal."

if (-not (Test-Path $buildRoot) -or -not (Test-Path $cachePath)) {
    throw "Expected existing build tree at $buildRoot. Configure the in-repo build once, then use this helper for normal debug build/run."
}

if ($KillRunning) {
    Stop-DawgProcess
}

Write-Step "Building DAWG ($Configuration)"
Invoke-Native -FailureMessage "CMake build failed." -Command {
    cmake --build $buildRoot --config $Configuration --target dawg
}

if (-not (Test-Path $exePath)) {
    throw "Build completed but the app executable was not found at $exePath"
}

Write-Step "Ready"
Write-Host "Build output: $exePath"

if ($Launch) {
    Write-Step "Starting DAWG"
    Push-Location $repoRoot
    try {
        & $exePath
        if ($LASTEXITCODE -ne 0) {
            throw "DAWG exited with code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

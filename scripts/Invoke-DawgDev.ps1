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

function Resolve-VcpkgRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $candidates = @(
        $env:DAWG_VCPKG_ROOT,
        $env:VCPKG_ROOT,
        (Join-Path $RepoRoot '.tools\vcpkg')
    )

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction SilentlyContinue
        if (-not $resolved) {
            continue
        }

        $toolchainPath = Join-Path $resolved.Path 'scripts\buildsystems\vcpkg.cmake'
        if (Test-Path $toolchainPath) {
            return $resolved.Path
        }
    }

    throw "Could not locate a usable vcpkg checkout. Set DAWG_VCPKG_ROOT or VCPKG_ROOT, or restore .tools\\vcpkg in the repo."
}

function Ensure-VcpkgReady {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VcpkgRoot
    )

    $vcpkgExePath = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (Test-Path $vcpkgExePath) {
        return
    }

    $bootstrapPath = Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat'
    if (-not (Test-Path $bootstrapPath)) {
        throw "vcpkg was found at $VcpkgRoot, but neither vcpkg.exe nor bootstrap-vcpkg.bat exists there."
    }

    Write-Step "Bootstrapping vcpkg"
    Invoke-Native -FailureMessage "vcpkg bootstrap failed." -Command {
        & $bootstrapPath
    }
}

function Test-BuildTreeConfigured {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildRoot,
        [Parameter(Mandatory = $true)]
        [string]$CachePath
    )

    if (-not (Test-Path $BuildRoot) -or -not (Test-Path $CachePath)) {
        return $false
    }

    $projectFiles = @(Get-ChildItem -Path $BuildRoot -Filter *.vcxproj -File -ErrorAction SilentlyContinue)
    if ($projectFiles.Count -gt 0) {
        return $true
    }

    $solutionFiles = @(Get-ChildItem -Path $BuildRoot -Filter *.sln -File -ErrorAction SilentlyContinue)
    return $solutionFiles.Count -gt 0
}

function Ensure-BuildTreeConfigured {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [Parameter(Mandatory = $true)]
        [string]$BuildRoot,
        [Parameter(Mandatory = $true)]
        [string]$CachePath,
        [Parameter(Mandatory = $true)]
        [string]$VcpkgRoot
    )

    if (Test-BuildTreeConfigured -BuildRoot $BuildRoot -CachePath $CachePath) {
        return
    }

    Write-Step "Configuring DAWG"
    Push-Location $RepoRoot
    try {
        $env:VCPKG_ROOT = $VcpkgRoot
        if ([string]::IsNullOrWhiteSpace($env:DAWG_VCPKG_ROOT)) {
            $env:DAWG_VCPKG_ROOT = $VcpkgRoot
        }

        Invoke-Native -FailureMessage "CMake configure failed." -Command {
            cmake --preset windows-msvc --fresh
        }
    }
    finally {
        Pop-Location
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$buildRoot = Join-Path $repoRoot 'build\windows-msvc-current'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$exePath = Join-Path $buildRoot "$Configuration\dawg.exe"
$vcpkgRoot = $null

Write-Step "Checking prerequisites"
Ensure-Command -Name cmake -InstallHint "Install CMake and re-open the terminal."
$vcpkgRoot = Resolve-VcpkgRoot -RepoRoot $repoRoot
Ensure-VcpkgReady -VcpkgRoot $vcpkgRoot
Ensure-BuildTreeConfigured -RepoRoot $repoRoot -BuildRoot $buildRoot -CachePath $cachePath -VcpkgRoot $vcpkgRoot

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

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

function Add-ToolToPath {
    param([string]$Directory)

    if ((Test-Path $Directory) -and ($env:Path -notlike "*$Directory*")) {
        $env:Path = "$Directory;$env:Path"
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

function Find-VsWhere {
    $candidate = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $candidate) {
        return $candidate
    }

    return $null
}

function Ensure-VisualStudio {
    $vsWhere = Find-VsWhere
    if (-not $vsWhere) {
        throw "Visual Studio Installer tools were not found. Install Visual Studio 2022 Community with Desktop development for C++."
    }

    $installationPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installationPath) {
        throw "Visual Studio 2022 with the C++ toolchain is required. Open Visual Studio Installer and add the Desktop development with C++ workload."
    }

    return $installationPath.Trim()
}

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Ensure-ShortPathVcpkg {
    param([string]$VcpkgRoot)

    $vcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
    if (Test-Path $vcpkgExe) {
        return
    }

    Ensure-Directory -Path $VcpkgRoot

    if (-not (Test-Path (Join-Path $VcpkgRoot ".git"))) {
        Write-Step "Cloning short-path vcpkg"
        Invoke-Native -FailureMessage "Failed to clone vcpkg." -Command {
            git clone https://github.com/microsoft/vcpkg $VcpkgRoot
        }
    }

    Write-Step "Bootstrapping short-path vcpkg"
    Invoke-Native -FailureMessage "Failed to bootstrap vcpkg." -Command {
        cmd /c (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat")
    }
}

function Sync-RepoToShortPath {
    param(
        [string]$Source,
        [string]$Destination
    )

    Ensure-Directory -Path $Destination

    $excludeDirs = @(
        '.git',
        '.vs',
        '.tools',
        'build',
        'b',
        'rel',
        'rel2',
        'out'
    )

    $roboArgs = @(
        $Source,
        $Destination,
        '/MIR',
        '/R:3',
        '/W:1',
        '/NFL',
        '/NDL',
        '/NJH',
        '/NJS',
        '/NP',
        '/XD'
    ) + $excludeDirs + @(
        '/XF',
        '*.user',
        '*.suo'
    )

    & robocopy @roboArgs | Out-Null
    $exitCode = $LASTEXITCODE
    if ($exitCode -gt 7) {
        throw "Sync to short-path workspace failed with robocopy exit code $exitCode."
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

$workspaceRoot = if ($env:DAWG_DEV_ROOT) { $env:DAWG_DEV_ROOT } else { 'C:\dawg-dev' }
$shortRepoRoot = Join-Path $workspaceRoot 'src'
$buildRoot = Join-Path $workspaceRoot 'out'
$vcpkgRoot = if ($env:DAWG_VCPKG_ROOT) { $env:DAWG_VCPKG_ROOT } else { 'C:\dv' }

Add-ToolToPath "C:\Program Files\CMake\bin"

Write-Step "Checking prerequisites"
Ensure-Command -Name git -InstallHint "Install Git for Windows and try again."
Ensure-Command -Name cmake -InstallHint "Install CMake and re-open the terminal."
$null = Ensure-VisualStudio

Write-Step "Preparing short-path workspace"
Sync-RepoToShortPath -Source $repoRoot -Destination $shortRepoRoot
Ensure-ShortPathVcpkg -VcpkgRoot $vcpkgRoot

$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_MAX_CONCURRENCY = '4'

if ($KillRunning) {
    Stop-DawgProcess
}

Write-Step "Configuring project"
Invoke-Native -FailureMessage "CMake configure failed." -Command {
    cmake -S $shortRepoRoot `
        -B $buildRoot `
        -G "Visual Studio 17 2022" `
        -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows `
        -DCMAKE_CXX_STANDARD=20
}

Write-Step "Building app ($Configuration)"
Invoke-Native -FailureMessage "CMake build failed." -Command {
    cmake --build $buildRoot --config $Configuration
}

$exePath = Join-Path $buildRoot "$Configuration\dawg.exe"
if (-not (Test-Path $exePath)) {
    throw "Build completed but the app executable was not found at $exePath"
}

Write-Step "Ready"
Write-Host "Short-path workspace: $shortRepoRoot"
Write-Host "Build output: $exePath"

if ($Launch) {
    Write-Step "Starting DAWG"
    Start-Process -FilePath $exePath -WorkingDirectory (Split-Path -Parent $exePath)
}

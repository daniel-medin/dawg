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

function Ensure-LocalVcpkg {
    param([string]$RepoRoot)

    $toolsRoot = Join-Path $RepoRoot ".tools"
    $vcpkgRoot = Join-Path $toolsRoot "vcpkg"
    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"

    if (-not (Test-Path $toolsRoot)) {
        New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
    }

    if (-not (Test-Path $vcpkgExe)) {
        Write-Step "Bootstrapping local vcpkg"
        if (-not (Test-Path $vcpkgRoot)) {
            git clone https://github.com/microsoft/vcpkg $vcpkgRoot
        }

        & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat")
    }

    return $vcpkgRoot
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
Set-Location $repoRoot

Add-ToolToPath "C:\Program Files\CMake\bin"

Write-Step "Checking prerequisites"
Ensure-Command -Name git -InstallHint "Install Git for Windows and try again."
Ensure-Command -Name cmake -InstallHint "Install CMake and re-open the terminal."
$null = Ensure-VisualStudio

$env:VCPKG_ROOT = Ensure-LocalVcpkg -RepoRoot $repoRoot

Write-Step "Configuring project"
cmake --preset windows-msvc

Write-Step "Building app"
cmake --build --preset windows-msvc-debug

$exePath = Join-Path $repoRoot "build\windows-msvc\Debug\dawg.exe"
if (-not (Test-Path $exePath)) {
    throw "Build completed but the app executable was not found at $exePath"
}

Write-Step "Starting DAWG"
Start-Process $exePath


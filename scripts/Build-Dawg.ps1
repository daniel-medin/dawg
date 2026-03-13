param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$helperPath = Join-Path $scriptRoot 'Invoke-DawgDev.ps1'

& $helperPath -Configuration $Configuration

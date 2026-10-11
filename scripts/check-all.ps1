param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'check-hygiene.ps1')
& (Join-Path $PSScriptRoot 'check-security.ps1')
& (Join-Path $PSScriptRoot 'test.ps1') -Configuration $Configuration

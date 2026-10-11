param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$StaticAnalysis
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Enter-MsvcEnvironment.ps1')

$buildSuffix = if ($StaticAnalysis) { 'analysis' } else { 'tests' }
$buildDirectory = Join-Path $repositoryRoot "out\$buildSuffix"
$analysisValue = if ($StaticAnalysis) { 'ON' } else { 'OFF' }

& cmake -S $repositoryRoot -B $buildDirectory -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    '-DBUILD_TESTING=ON' `
    "-DTOOLSBOX_ENABLE_MSVC_ANALYSIS=$analysisValue"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

if ($StaticAnalysis) {
    & cmake --build $buildDirectory --target payload PhasmophobiaInjector pe_embed
    if ($LASTEXITCODE -ne 0) { throw 'Static-analysis build failed.' }
}
else {
    & cmake --build $buildDirectory --target check
    if ($LASTEXITCODE -ne 0) { throw 'Build or tests failed.' }
    & (Join-Path $PSScriptRoot 'embed-game-tests.ps1')
}

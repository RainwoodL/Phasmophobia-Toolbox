param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+(?:[.-][A-Za-z0-9]+)*$')]
    [string]$Version,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    $sourceCommit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
    $workingChanges = @(& git status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $workingChanges.Count -ne 0) {
        throw 'Commit source changes before packaging a release.'
    }

    $outputDirectory = Join-Path $repositoryRoot "out\release-packages\$Version"
    if (Test-Path -LiteralPath $outputDirectory) {
        throw "Release package directory already exists: $outputDirectory"
    }
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'check-all.ps1') -Configuration Release
    }

    $packageName = "Phasmophobia-Toolbox-$Version-win-x64"
    $packageDirectory = Join-Path $outputDirectory $packageName
    $fileMap = [ordered]@{
        'LICENSE' = 'LICENSE'
        'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
        'imgui/LICENSE.txt' = 'imgui/LICENSE.txt'
        'third_party/detours/LICENSE.md' = 'third_party/detours/LICENSE.md'
        'README.md' = 'docs/release-embedded.md'
        'install-embedded.cmd' = 'scripts/install-embedded.cmd'
        'scripts/install-embedded.ps1' = 'scripts/install-embedded.ps1'
        'scripts/embed-game.ps1' = 'scripts/embed-game.ps1'
        'release/standalone/PhasmophobiaInjector.exe' = 'release/standalone/PhasmophobiaInjector.exe'
        'release/embedded/payload.dll' = 'release/embedded/payload.dll'
        'release/tools/pe_embed.exe' = 'release/tools/pe_embed.exe'
    }
    foreach ($relativePath in $fileMap.Values) {
        if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot $relativePath) -PathType Leaf)) {
            throw "Release file missing: $relativePath"
        }
    }
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null

    $fileRecords = @(foreach ($entry in $fileMap.GetEnumerator()) {
        $sourcePath = Join-Path $repositoryRoot $entry.Value
        $destinationPath = Join-Path $packageDirectory $entry.Key
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
        if ([IO.Path]::GetExtension($destinationPath) -eq '.ps1') {
            [IO.File]::WriteAllText($destinationPath, [IO.File]::ReadAllText($sourcePath), [Text.UTF8Encoding]::new($true))
        }
        else {
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
        }
        [ordered]@{
            Path = $entry.Key
            Bytes = (Get-Item -LiteralPath $destinationPath).Length
            SHA256 = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    $manifest = [ordered]@{
        Version = $Version
        Mode = 'Combined'
        Configuration = 'Release'
        Architecture = 'x64'
        SourceCommit = $sourceCommit
        CreatedAtUtc = [DateTime]::UtcNow.ToString('o')
        Files = $fileRecords
    }
    $manifestPath = Join-Path $packageDirectory 'build-manifest.json'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $checksums = @($fileRecords | ForEach-Object { "$($_.SHA256)  $($_.Path)" })
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksums += "$manifestHash  build-manifest.json"
    $checksums | Set-Content -LiteralPath (Join-Path $packageDirectory 'SHA256SUMS.txt') -Encoding ASCII

    $archivePath = Join-Path $outputDirectory ($packageName + '.zip')
    Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archivePath -CompressionLevel Optimal

    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$archiveHash  $packageName.zip" | Set-Content -LiteralPath (Join-Path $outputDirectory 'SHA256SUMS.txt') -Encoding ASCII
    Write-Output "Release package: $archivePath"
    Get-Item -LiteralPath $archivePath | Select-Object Name, Length
}
finally {
    Pop-Location
}

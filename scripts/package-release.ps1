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
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'check-all.ps1') -Configuration Release
    }

    $outputDirectory = Join-Path $repositoryRoot "out\release-packages\$Version"
    if (Test-Path -LiteralPath $outputDirectory) {
        throw "Release package directory already exists: $outputDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    $artifactPaths = [ordered]@{
        Injector = 'release/standalone/PhasmophobiaInjector.exe'
        Payload = 'release/embedded/payload.dll'
        Embedder = 'release/tools/pe_embed.exe'
    }
    foreach ($relativePath in $artifactPaths.Values) {
        if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot $relativePath) -PathType Leaf)) {
            throw "Release artifact missing: $relativePath"
        }
    }

    $archives = [Collections.Generic.List[string]]::new()
    foreach ($mode in @('Injector', 'Embedded')) {
        $packageName = "Phasmophobia-Toolbox-$Version-$mode-win-x64"
        $packageDirectory = Join-Path $outputDirectory $packageName
        $fileMap = [ordered]@{
            'LICENSE' = 'LICENSE'
            'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
            'imgui/LICENSE.txt' = 'imgui/LICENSE.txt'
            'third_party/detours/LICENSE.md' = 'third_party/detours/LICENSE.md'
        }
        if ($mode -eq 'Injector') {
            $fileMap['README.md'] = 'docs/release-injector.md'
            $fileMap['PhasmophobiaInjector.exe'] = $artifactPaths.Injector
        }
        else {
            $fileMap['README.md'] = 'docs/release-embedded.md'
            $fileMap['install-embedded.cmd'] = 'scripts/install-embedded.cmd'
            $fileMap['scripts/install-embedded.ps1'] = 'scripts/install-embedded.ps1'
            $fileMap['scripts/embed-game.ps1'] = 'scripts/embed-game.ps1'
            foreach ($relativePath in $artifactPaths.Values) {
                $fileMap[$relativePath] = $relativePath
            }
        }

        foreach ($entry in $fileMap.GetEnumerator()) {
            $sourcePath = Join-Path $repositoryRoot $entry.Value
            $destinationPath = Join-Path $packageDirectory $entry.Key
            New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
            if ([IO.Path]::GetExtension($destinationPath) -eq '.ps1') {
                [IO.File]::WriteAllText($destinationPath, [IO.File]::ReadAllText($sourcePath), [Text.UTF8Encoding]::new($true))
            }
            else {
                Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
            }
        }

        $fileRecords = @($fileMap.Keys | ForEach-Object {
            $filePath = Join-Path $packageDirectory $_
            [ordered]@{
                Path = $_
                Bytes = (Get-Item -LiteralPath $filePath).Length
                SHA256 = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        })
        $manifest = [ordered]@{
            Version = $Version
            Mode = $mode
            Configuration = 'Release'
            Architecture = 'x64'
            SourceCommit = $sourceCommit
            CreatedAtUtc = [DateTime]::UtcNow.ToString('o')
            Files = $fileRecords
        }
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $packageDirectory 'build-manifest.json') -Encoding UTF8
        $checksums = @($fileRecords | ForEach-Object { "$($_.SHA256)  $($_.Path)" })
        $manifestHash = (Get-FileHash -LiteralPath (Join-Path $packageDirectory 'build-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        $checksums += "$manifestHash  build-manifest.json"
        $checksums | Set-Content -LiteralPath (Join-Path $packageDirectory 'SHA256SUMS.txt') -Encoding ASCII

        $archivePath = Join-Path $outputDirectory ($packageName + '.zip')
        Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archivePath -CompressionLevel Optimal
        $archives.Add($archivePath)
    }

    $archives | ForEach-Object {
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($_)
    } | Set-Content -LiteralPath (Join-Path $outputDirectory 'SHA256SUMS.txt') -Encoding ASCII
    Write-Output "Release packages: $outputDirectory"
    $archives | ForEach-Object { Get-Item -LiteralPath $_ | Select-Object Name, Length }
}
finally {
    Pop-Location
}

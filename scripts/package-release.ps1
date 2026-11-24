param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+(?:[.-][A-Za-z0-9]+)*$')]
    [string]$Version,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$stageDirectory = $null
$packageLock = $null
Push-Location $repositoryRoot
try {
    $sourceCommit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
    $workingChanges = @(& git status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $workingChanges.Count -ne 0) {
        throw 'Commit source changes before packaging a release.'
    }

    $outputDirectory = Join-Path $repositoryRoot "out\release-packages\$Version"
    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    $packageLock = [IO.FileStream]::new((Join-Path $outputDirectory '.packaging.lock'),
        [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None,
        4096, [IO.FileOptions]::DeleteOnClose)
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'check-all.ps1') -Configuration Release
    }

    $archiveName = "Phasmophobia-Toolbox-$Version-win-x64.zip"
    $fileMap = [ordered]@{
        'Toolsbox.exe' = 'release/standalone/PhasmophobiaInjector.exe'
        'Phasmophobia.exe' = 'release/embedded/Phasmophobia.exe'
    }
    $stageName = '.stage-' + [Guid]::NewGuid().ToString('N')
    $stageDirectory = Join-Path $outputDirectory $stageName
    New-Item -ItemType Directory -Path $stageDirectory | Out-Null
    $fileRecords = @(foreach ($entry in $fileMap.GetEnumerator()) {
        $stagedFile = Join-Path $stageDirectory $entry.Key
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $entry.Value) -Destination $stagedFile
        [ordered]@{
            Path = $entry.Key
            Bytes = (Get-Item -LiteralPath $stagedFile).Length
            SHA256 = (Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })

    $archivePath = Join-Path $stageDirectory $archiveName
    $stagedExecutables = @($fileMap.Keys | ForEach-Object { Join-Path $stageDirectory $_ })
    Compress-Archive -LiteralPath $stagedExecutables -DestinationPath $archivePath -CompressionLevel Optimal
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = [ordered]@{
        Version = $Version
        Configuration = 'Release'
        Architecture = 'x64'
        Mode = 'Combined'
        SourceCommit = $sourceCommit
        CreatedAtUtc = [DateTime]::UtcNow.ToString('o')
        Files = $fileRecords
        Archive = [ordered]@{ Path = $archiveName; SHA256 = $archiveHash }
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stageDirectory 'build-manifest.json') -Encoding UTF8
    "$archiveHash  $archiveName" | Set-Content -LiteralPath (Join-Path $stageDirectory 'SHA256SUMS.txt') -Encoding ASCII

    # Publish the archive and its build records together, rolling back on failure.
    . (Join-Path $PSScriptRoot 'embed-game.ps1')
    $outputs = @(foreach ($name in @('build-manifest.json', 'SHA256SUMS.txt', $archiveName)) {
        @{ Source = Join-Path $stageDirectory $name; Destination = Join-Path $outputDirectory $name }
    })
    Install-EmbedFiles -Files $outputs
    Write-Output "Release ZIP SHA-256: $archiveHash"
    Get-Item -LiteralPath (Join-Path $outputDirectory $archiveName) | Select-Object Name, Length
}
finally {
    try {
        if ($stageDirectory -and (Test-Path -LiteralPath $stageDirectory -PathType Container)) {
            # Delete only our known temporary files. Leave any unexpected entries intact.
            foreach ($name in @('Toolsbox.exe', 'Phasmophobia.exe', $archiveName, 'build-manifest.json', 'SHA256SUMS.txt')) {
                [IO.File]::Delete((Join-Path $stageDirectory $name))
            }
            [IO.Directory]::Delete($stageDirectory)
        }
    }
    finally {
        if ($packageLock) { $packageLock.Dispose() }
        Pop-Location
    }
}

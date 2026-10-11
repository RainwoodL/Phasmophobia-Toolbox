param(
    [string]$GameExecutable = 'D:\steam\steamapps\common\Phasmophobia\Phasmophobia.exe',
    [switch]$PackageOnly,
    [string]$DumpPath = '',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-EmbedFileRecord {
    param([string]$Path)
    $hash = $null
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
    return [ordered]@{ Path = $Path; SHA256 = $hash }
}

function Test-EmbedPeSection {
    param([string]$Path)
    $data = [IO.File]::ReadAllBytes($Path)
    if ($data.Length -lt 64 -or [BitConverter]::ToUInt16($data, 0) -ne 0x5A4D) {
        throw "Invalid DOS executable: $Path"
    }
    $pe = [BitConverter]::ToInt32($data, 60)
    if ($pe -lt 0 -or $pe -gt $data.Length - 24 -or
        [BitConverter]::ToUInt32($data, $pe) -ne 0x4550) {
        throw "Invalid PE executable: $Path"
    }
    $count = [BitConverter]::ToUInt16($data, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($data, $pe + 20)
    $table = $pe + 24 + $optionalSize
    if ([BitConverter]::ToUInt16($data, $pe + 4) -ne 0x8664 -or
        $count -eq 0 -or $optionalSize -lt 2 -or
        [uint64]$table + [uint64]$count * 40 -gt $data.Length -or
        [BitConverter]::ToUInt16($data, $pe + 24) -ne 0x20B) {
        throw "Invalid x64 PE section table: $Path"
    }
    for ($index = 0; $index -lt $count; $index++) {
        # Match pe_embed's four-byte prefix check, including malformed .tbx suffixes.
        if ([Text.Encoding]::ASCII.GetString($data, $table + $index * 40, 4) -eq '.tbx') {
            return $true
        }
    }
    return $false
}

function Build-EmbedArtifacts {
    param([string]$RepositoryRoot, [string]$BuildDirectory, [string]$ReleaseDirectory)
    . (Join-Path $PSScriptRoot 'Enter-MsvcEnvironment.ps1')
    & cmake -S $RepositoryRoot -B $BuildDirectory -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTING=OFF' "-DTOOLSBOX_RELEASE_DIR=$ReleaseDirectory"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & cmake --build $BuildDirectory --target payload PhasmophobiaInjector pe_embed
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}

function New-EmbedCandidate {
    param([string]$Embedder, [string]$InputExe, [string]$Payload,
          [string]$OutputExe, [string]$RuntimePayload)
    & $Embedder $InputExe $Payload $OutputExe $RuntimePayload
    if ($LASTEXITCODE -ne 0) { throw 'PE embedding failed.' }
}

function Install-EmbedFiles {
    param([object[]]$Files)
    $journal = [Collections.Generic.List[object]]::new()
    $cleanBackups = $false
    try {
        # Prepare every replacement beside its destination for same-volume atomic rename.
        foreach ($file in $Files) {
            $suffix = '.toolsbox-' + [Guid]::NewGuid().ToString('N')
            $entry = [pscustomobject]@{
                Destination = $file.Destination
                Temporary = $file.Destination + $suffix + '.tmp'
                Rollback = $file.Destination + $suffix + '.bak'
                Existed = Test-Path -LiteralPath $file.Destination -PathType Leaf
                PreviousHash = (Get-EmbedFileRecord $file.Destination).SHA256
                Committed = $false
                Hash = (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash
            }
            $journal.Add($entry)
            New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Destination) -Force | Out-Null
            Copy-Item -LiteralPath $file.Source -Destination $entry.Temporary
            if ((Get-FileHash -LiteralPath $entry.Temporary -Algorithm SHA256).Hash -ne $entry.Hash) {
                throw "Replacement copy verification failed: $($entry.Destination)"
            }
        }
        foreach ($entry in $journal) {
            if ($entry.Existed) {
                [IO.File]::Replace($entry.Temporary, $entry.Destination, $entry.Rollback, $true)
            } else {
                [IO.File]::Move($entry.Temporary, $entry.Destination)
            }
            $entry.Committed = $true
            if ((Get-FileHash -LiteralPath $entry.Destination -Algorithm SHA256).Hash -ne $entry.Hash) {
                throw "Installed file verification failed: $($entry.Destination)"
            }
        }
        $cleanBackups = $true
    }
    catch {
        $failure = $_
        $rollbackErrors = [Collections.Generic.List[string]]::new()
        for ($index = $journal.Count - 1; $index -ge 0; $index--) {
            $entry = $journal[$index]
            if (-not $entry.Committed) { continue }
            try {
                if ($entry.Existed) {
                    [IO.File]::Replace($entry.Rollback, $entry.Destination, $entry.Temporary, $true)
                    if ((Get-FileHash -LiteralPath $entry.Destination -Algorithm SHA256).Hash -ne $entry.PreviousHash) {
                        throw "Restored file verification failed: $($entry.Destination)"
                    }
                } else {
                    Remove-Item -LiteralPath $entry.Destination -Force
                }
            }
            catch { $rollbackErrors.Add("$($entry.Rollback): $($_.Exception.Message)") }
        }
        $cleanBackups = $rollbackErrors.Count -eq 0
        if (-not $cleanBackups) {
            throw "$($failure.Exception.Message) Rollback incomplete; retained backup(s): $($rollbackErrors -join '; ')"
        }
        throw $failure
    }
    finally {
        foreach ($entry in $journal) {
            if (Test-Path -LiteralPath $entry.Temporary -PathType Leaf) {
                Remove-Item -LiteralPath $entry.Temporary -Force -ErrorAction SilentlyContinue
            }
            if ($cleanBackups -and (Test-Path -LiteralPath $entry.Rollback -PathType Leaf)) {
                Remove-Item -LiteralPath $entry.Rollback -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

function Invoke-EmbedGame {
    param(
        [string]$GameExecutable,
        [switch]$PackageOnly,
        [string]$DumpPath = '',
        [switch]$SkipBuild,
        [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
    )
    $gameExecutable = [IO.Path]::GetFullPath($GameExecutable)
    $gameDirectory = Split-Path -Parent $gameExecutable
    if (-not (Test-Path -LiteralPath $gameExecutable -PathType Leaf)) {
        throw "Game executable was not found: $gameExecutable"
    }
    # Reject repeated embedding before loading build tools or changing any artifacts.
    if (Test-EmbedPeSection -Path $gameExecutable) {
        throw "Game executable already contains a .tbx section: $gameExecutable"
    }
    if (-not $PackageOnly -and (Get-Process -Name Phasmophobia -ErrorAction SilentlyContinue)) {
        throw 'Phasmophobia must be closed before deploying ToolsBOX.'
    }
    if ([string]::IsNullOrWhiteSpace($DumpPath)) { $DumpPath = Join-Path $RepositoryRoot 'dump.cs' }
    $sources = [ordered]@{
        GameExecutable = Get-EmbedFileRecord $gameExecutable
        GameAssembly = Get-EmbedFileRecord (Join-Path $gameDirectory 'GameAssembly.dll')
        Metadata = Get-EmbedFileRecord (Join-Path $gameDirectory 'Phasmophobia_Data\il2cpp_data\Metadata\global-metadata.dat')
        Dump = Get-EmbedFileRecord ([IO.Path]::GetFullPath($DumpPath))
    }
    $releaseDirectory = Join-Path $RepositoryRoot 'release'
    $buildDirectory = Join-Path $RepositoryRoot 'out\release'
    $runtimePayload = Join-Path $gameDirectory '.toolsbox.payload.dll'
    $embeddedExecutable = Join-Path $releaseDirectory 'embedded\Phasmophobia.embedded.exe'
    $manifestPath = Join-Path $releaseDirectory 'embedded\embed-manifest.json'
    $stageName = '.embed-stage-' + [Guid]::NewGuid().ToString('N')
    $stageDirectory = Join-Path $releaseDirectory $stageName
    $stageCandidate = Join-Path $stageDirectory 'Phasmophobia.embedded.exe'
    $stageManifest = Join-Path $stageDirectory 'embed-manifest.json'
    $backup = $null
    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    try {
        if (-not $SkipBuild) {
            Build-EmbedArtifacts $RepositoryRoot $buildDirectory $releaseDirectory | Out-Host
        }
        $payload = Join-Path $releaseDirectory 'embedded\payload.dll'
        $embedder = Join-Path $releaseDirectory 'tools\pe_embed.exe'
        $standalone = Join-Path $releaseDirectory 'standalone\PhasmophobiaInjector.exe'
        foreach ($required in @($payload, $embedder, $standalone)) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required Release artifact is missing: $required"
            }
        }
        New-EmbedCandidate $embedder $gameExecutable $payload $stageCandidate $runtimePayload | Out-Host
        if ((Get-Item -LiteralPath $stageCandidate).Length -le (Get-Item -LiteralPath $gameExecutable).Length -or
            -not (Test-EmbedPeSection -Path $stageCandidate)) {
            throw 'Embedded candidate failed size or .tbx section validation.'
        }
        $embeddedHash = (Get-FileHash -LiteralPath $stageCandidate -Algorithm SHA256).Hash
        foreach ($source in $sources.Values) {
            if ((Get-EmbedFileRecord $source.Path).SHA256 -ne $source.SHA256) {
                throw "Source changed while building the package: $($source.Path)"
            }
        }
        $manifest = [ordered]@{
            SchemaVersion = 1
            CreatedAtUtc = [DateTime]::UtcNow.ToString('o')
            PackageOnly = [bool]$PackageOnly
            Deployed = -not [bool]$PackageOnly
            Sources = $sources
            Payload = Get-EmbedFileRecord $payload
            StandaloneInjector = Get-EmbedFileRecord $standalone
            EmbeddedExecutable = [ordered]@{ Path = $embeddedExecutable; SHA256 = $embeddedHash }
            RuntimePayload = $runtimePayload
            Backup = $null
        }
        if (-not $PackageOnly) {
            if (Get-Process -Name Phasmophobia -ErrorAction SilentlyContinue) {
                throw 'Phasmophobia started during the build; deployment stopped.'
            }
            $backupDirectory = Join-Path $releaseDirectory 'backups'
            New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
            $backup = Join-Path $backupDirectory ('Phasmophobia.original-' +
                (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '-' + [Guid]::NewGuid().ToString('N') + '.exe')
            Copy-Item -LiteralPath $gameExecutable -Destination $backup
            if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $sources.GameExecutable.SHA256) {
                throw 'Original executable backup verification failed.'
            }
            $manifest.Backup = $backup
        }
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stageManifest -Encoding UTF8
        $files = @(
            @{ Source = $stageCandidate; Destination = $embeddedExecutable },
            @{ Source = $stageManifest; Destination = $manifestPath }
        )
        if (-not $PackageOnly) { $files += @{ Source = $stageCandidate; Destination = $gameExecutable } }
        Install-EmbedFiles -Files $files
        # Keep any runtime DLL untouched. The embedded bootstrap rewrites it at the next launch.
        [pscustomobject]@{
            GameExecutable = $gameExecutable
            StandaloneInjector = $standalone
            EmbeddedExecutable = $embeddedExecutable
            Manifest = $manifestPath
            OriginalSHA256 = $sources.GameExecutable.SHA256
            EmbeddedSHA256 = $embeddedHash
            Backup = $backup
            RuntimePayload = $runtimePayload
            PackageOnly = [bool]$PackageOnly
        }
    }
    finally {
        $resolvedStage = [IO.Path]::GetFullPath($stageDirectory)
        $resolvedRelease = [IO.Path]::GetFullPath($releaseDirectory)
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
                [IO.Path]::GetDirectoryName($resolvedStage), $resolvedRelease) -or
            [IO.Path]::GetFileName($resolvedStage) -ne $stageName) {
            throw 'Refusing to clean an unexpected embedding stage directory.'
        }
        if (Test-Path -LiteralPath $stageDirectory -PathType Container) {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# Dot-sourcing exposes the workflow to the isolated script regression tests.
if ($MyInvocation.InvocationName -ne '.') {
    Invoke-EmbedGame -GameExecutable $GameExecutable -PackageOnly:$PackageOnly -DumpPath $DumpPath -SkipBuild:$SkipBuild
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'embed-game.ps1')
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out\embed-script-tests'))
$testRoot = Join-Path $testParent ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$script:buildCount = 0
$script:embedMode = 'success'
$script:changeSource = $false
$passed = 0

function Assert-EmbedTest {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "FAIL: $Message" }
}

function New-TestPe {
    param([bool]$Embedded)
    $data = [byte[]]::new($(if ($Embedded) { 1024 } else { 512 }))
    [BitConverter]::GetBytes([uint16]0x5A4D).CopyTo($data, 0)
    [BitConverter]::GetBytes([int32]128).CopyTo($data, 60)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($data, 128)
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($data, 132)
    [BitConverter]::GetBytes([uint16]$(if ($Embedded) { 2 } else { 1 })).CopyTo($data, 134)
    [BitConverter]::GetBytes([uint16]240).CopyTo($data, 148)
    [BitConverter]::GetBytes([uint16]0x20B).CopyTo($data, 152)
    [Text.Encoding]::ASCII.GetBytes('.text').CopyTo($data, 392)
    if ($Embedded) { [Text.Encoding]::ASCII.GetBytes('.tbx').CopyTo($data, 432) }
    return ,$data
}

function New-EmbedFixture {
    param([string]$Name)
    $fixtureRoot = Join-Path $testRoot $Name
    $repo = Join-Path $fixtureRoot 'repo'
    $gameDirectory = Join-Path $fixtureRoot 'game'
    $metadata = Join-Path $gameDirectory 'Phasmophobia_Data\il2cpp_data\Metadata\global-metadata.dat'
    $game = Join-Path $gameDirectory 'Phasmophobia.exe'
    $embedded = Join-Path $repo 'release\embedded\Phasmophobia.embedded.exe'
    $manifest = Join-Path $repo 'release\embedded\embed-manifest.json'
    $runtime = Join-Path $gameDirectory '.toolsbox.payload.dll'
    $artifacts = @(
        (Join-Path $repo 'release\embedded\payload.dll'),
        (Join-Path $repo 'release\tools\pe_embed.exe'),
        (Join-Path $repo 'release\standalone\PhasmophobiaInjector.exe'),
        $embedded, $manifest, $runtime, $metadata,
        (Join-Path $gameDirectory 'GameAssembly.dll'), (Join-Path $repo 'dump.cs')
    )
    foreach ($path in $artifacts) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [IO.File]::WriteAllText($path, 'original-' + [IO.Path]::GetFileName($path))
    }
    [IO.File]::WriteAllBytes($game, (New-TestPe $false))
    return [pscustomobject]@{
        Repository = $repo; Game = $game; Embedded = $embedded; Manifest = $manifest; Runtime = $runtime
    }
}

function Get-FixtureHashes {
    param($Fixture)
    return (@($Fixture.Game, $Fixture.Embedded, $Fixture.Manifest, $Fixture.Runtime) |
        ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }) -join ','
}

# Replace only the native build/embed boundaries. File preparation, verification,
# manifests, real atomic replacements and rollback all run through production code.
function Build-EmbedArtifacts {
    param([string]$RepositoryRoot, [string]$BuildDirectory, [string]$ReleaseDirectory)
    $script:buildCount++
    if ($script:changeSource) {
        [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $RepositoryRoot) 'game\GameAssembly.dll'), 'changed')
    }
}
function New-EmbedCandidate {
    param([string]$Embedder, [string]$InputExe, [string]$Payload,
          [string]$OutputExe, [string]$RuntimePayload)
    if ($script:embedMode -eq 'fail') { throw 'Simulated embedder failure.' }
    $data = New-TestPe ($script:embedMode -ne 'invalid')
    [IO.File]::WriteAllBytes($OutputExe, $data)
}
function Get-Process {
    param([string]$Name, [string]$ErrorAction)
    return $null
}

try {
    $fixture = New-EmbedFixture 'prepatched'
    [IO.File]::WriteAllBytes($fixture.Game, (New-TestPe $true))
    $before = Get-FixtureHashes $fixture
    $buildsBefore = $script:buildCount
    $rejected = $false
    try { Invoke-EmbedGame -GameExecutable $fixture.Game -RepositoryRoot $fixture.Repository | Out-Null }
    catch { $rejected = $_.Exception.Message -like '*.tbx*' }
    Assert-EmbedTest $rejected 'already-embedded input must be rejected'
    Assert-EmbedTest ($script:buildCount -eq $buildsBefore) 'precheck must run before building'
    Assert-EmbedTest ((Get-FixtureHashes $fixture) -eq $before) 'precheck changed existing files'
    $passed++

    foreach ($mode in @('fail', 'invalid')) {
        $fixture = New-EmbedFixture $mode
        $before = Get-FixtureHashes $fixture
        $script:embedMode = $mode
        $rejected = $false
        try { Invoke-EmbedGame -GameExecutable $fixture.Game -RepositoryRoot $fixture.Repository | Out-Null }
        catch { $rejected = $true }
        Assert-EmbedTest $rejected "$mode candidate must be rejected"
        Assert-EmbedTest ((Get-FixtureHashes $fixture) -eq $before) "$mode changed previous package or game"
        Assert-EmbedTest (@(Get-ChildItem -LiteralPath (Join-Path $fixture.Repository 'release') -Filter '.embed-stage-*').Count -eq 0) 'failed stage was not cleaned'
        $passed++
    }
    $script:embedMode = 'success'

    $fixture = New-EmbedFixture 'package-only'
    $gameHash = (Get-FileHash -LiteralPath $fixture.Game).Hash
    $runtimeHash = (Get-FileHash -LiteralPath $fixture.Runtime).Hash
    $result = Invoke-EmbedGame -GameExecutable $fixture.Game -RepositoryRoot $fixture.Repository -PackageOnly
    $manifest = Get-Content -LiteralPath $result.Manifest -Raw | ConvertFrom-Json
    Assert-EmbedTest ((Get-FileHash -LiteralPath $fixture.Game).Hash -eq $gameHash) 'PackageOnly changed the game'
    Assert-EmbedTest ((Get-FileHash -LiteralPath $fixture.Runtime).Hash -eq $runtimeHash) 'PackageOnly changed runtime DLL'
    Assert-EmbedTest ($null -eq $result.Backup) 'PackageOnly created a deployment backup'
    Assert-EmbedTest ($manifest.PackageOnly -and -not $manifest.Deployed) 'PackageOnly manifest mode is wrong'
    foreach ($source in @('GameExecutable', 'GameAssembly', 'Metadata', 'Dump')) {
        $record = $manifest.Sources.$source
        Assert-EmbedTest ($record.SHA256 -eq (Get-FileHash -LiteralPath $record.Path).Hash) "incorrect source hash: $source"
    }
    foreach ($artifact in @('Payload', 'StandaloneInjector', 'EmbeddedExecutable')) {
        $record = $manifest.$artifact
        Assert-EmbedTest ($record.SHA256 -eq (Get-FileHash -LiteralPath $record.Path).Hash) "incorrect artifact hash: $artifact"
    }
    $passed++

    $fixture = New-EmbedFixture 'deploy'
    $gameHash = (Get-FileHash -LiteralPath $fixture.Game).Hash
    $runtimeHash = (Get-FileHash -LiteralPath $fixture.Runtime).Hash
    $result = Invoke-EmbedGame -GameExecutable $fixture.Game -RepositoryRoot $fixture.Repository
    $manifest = Get-Content -LiteralPath $result.Manifest -Raw | ConvertFrom-Json
    Assert-EmbedTest ((Get-FileHash -LiteralPath $fixture.Game).Hash -eq $result.EmbeddedSHA256) 'deployment bytes differ from package'
    Assert-EmbedTest ((Get-FileHash -LiteralPath $result.Backup).Hash -eq $gameHash) 'deployment backup differs from original'
    Assert-EmbedTest ((Get-FileHash -LiteralPath $fixture.Runtime).Hash -eq $runtimeHash) 'runtime DLL was deleted or overwritten before launch'
    Assert-EmbedTest ($manifest.Deployed -and -not $manifest.PackageOnly) 'deployed manifest mode is wrong'
    $passed++

    $fixture = New-EmbedFixture 'source-change'
    $before = Get-FixtureHashes $fixture
    $script:changeSource = $true
    $rejected = $false
    try { Invoke-EmbedGame -GameExecutable $fixture.Game -RepositoryRoot $fixture.Repository -PackageOnly | Out-Null }
    catch { $rejected = $_.Exception.Message -like '*Source changed*' }
    $script:changeSource = $false
    Assert-EmbedTest $rejected 'changed game module must invalidate the package'
    Assert-EmbedTest ((Get-FixtureHashes $fixture) -eq $before) 'source change replaced existing artifacts'
    $passed++

    $fixture = New-EmbedFixture 'rollback'
    $originalHash = (Get-FileHash -LiteralPath $fixture.Embedded).Hash
    $newSource = Join-Path $fixture.Repository 'new-package.bin'
    [IO.File]::WriteAllText($newSource, 'new-package')
    # A directory at the second destination forces a real Move failure after
    # the first File.Replace has already succeeded.
    $blockedDestination = Join-Path $fixture.Repository 'blocked-target'
    New-Item -ItemType Directory -Path $blockedDestination | Out-Null
    $rejected = $false
    try {
        Install-EmbedFiles -Files @(
            @{ Source = $newSource; Destination = $fixture.Embedded },
            @{ Source = $newSource; Destination = $blockedDestination }
        )
    }
    catch { $rejected = $true }
    Assert-EmbedTest $rejected 'blocked destination must fail the transaction'
    Assert-EmbedTest ((Get-FileHash -LiteralPath $fixture.Embedded).Hash -eq $originalHash) 'earlier replacement was not rolled back'
    Assert-EmbedTest (@(Get-ChildItem -LiteralPath $fixture.Repository -Recurse -File |
        Where-Object { $_.Name -match '\.toolsbox-.*\.(tmp|bak)$' }).Count -eq 0) 'rollback left temporary files'
    $passed++

    Write-Output "Embedding script regression tests passed ($passed scenarios)."
}
finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedTestRoot.StartsWith($testParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a test directory outside the intended workspace.'
    }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}

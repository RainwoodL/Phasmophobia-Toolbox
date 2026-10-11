Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    $trackedFiles = @(& git -c core.quotepath=false ls-files)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }

    $errors = [System.Collections.Generic.List[string]]::new()
    $forbiddenPaths = @(
        '^(out|build|release|\.vs)/',
        '^tools/Il2CppDumper/'
    )
    $forbiddenExtensions = @('.dll', '.exe', '.ilk', '.lib', '.obj', '.pdb', '.pfx', '.p12', '.pem', '.key')
    $textExtensions = @('.c', '.cc', '.cmake', '.cpp', '.h', '.hpp', '.json', '.md', '.ps1', '.rc', '.txt', '.yml', '.yaml')
    $maximumFileBytes = 5MB

    foreach ($relativePath in $trackedFiles) {
        $normalizedPath = $relativePath.Replace('\', '/')
        $absolutePath = Join-Path $repositoryRoot $relativePath
        if (-not [IO.File]::Exists($absolutePath)) { continue }

        foreach ($pattern in $forbiddenPaths) {
            if ($normalizedPath -match $pattern) {
                $errors.Add("generated/local artifact is tracked: $relativePath")
            }
        }

        $extension = [IO.Path]::GetExtension($relativePath).ToLowerInvariant()
        if ($forbiddenExtensions -contains $extension) {
            $errors.Add("binary or sensitive file type is tracked: $relativePath")
        }

        if ((Get-Item -LiteralPath $absolutePath).Length -gt $maximumFileBytes) {
            $errors.Add("file exceeds 5 MiB: $relativePath")
        }

        $isCMakeLists = [IO.Path]::GetFileName($relativePath) -eq 'CMakeLists.txt'
        $isVendored = $normalizedPath -match '^(imgui|third_party)/'
        if ((($textExtensions -contains $extension) -or $isCMakeLists) -and -not $isVendored) {
            $lineNumber = 0
            foreach ($line in [IO.File]::ReadLines($absolutePath)) {
                $lineNumber++
                if ($line -match '[ \t]+$') {
                    $errors.Add("trailing whitespace: ${relativePath}:$lineNumber")
                }
                if ($line -match '^(<<<<<<<|=======|>>>>>>>)') {
                    $errors.Add("merge-conflict marker: ${relativePath}:$lineNumber")
                }
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Output "ERROR: $_" }
        throw "Repository hygiene check failed with $($errors.Count) issue(s)."
    }

    Write-Output "Repository hygiene check passed ($($trackedFiles.Count) tracked files)."
}
finally {
    Pop-Location
}

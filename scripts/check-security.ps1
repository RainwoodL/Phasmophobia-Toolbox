Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    $trackedFiles = @(& git -c core.quotepath=false ls-files)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }

    $errors = [System.Collections.Generic.List[string]]::new()
    $sensitiveNames = @('.env', 'id_rsa', 'id_ed25519')
    $textExtensions = @('.c', '.cc', '.cmake', '.cpp', '.h', '.hpp', '.json', '.md', '.ps1', '.rc', '.txt', '.yml', '.yaml')
    $secretPatterns = [ordered]@{
        'private key' = ('-----BEGIN ' + '(?:OPENSSH|RSA|EC|DSA) PRIVATE KEY-----')
        'GitHub token' = '\bgh[pousr]_[A-Za-z0-9_]{30,}\b'
        'AWS access key' = '\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'
        'Google API key' = '\bAIza[0-9A-Za-z_-]{35}\b'
        'Slack token' = '\bxox[baprs]-[A-Za-z0-9-]{20,}\b'
        'generic assigned secret' = '(?i)\b(?:api[_-]?key|client[_-]?secret|access[_-]?token|auth[_-]?token|password)\b\s*[:=]\s*["''][^"'']{12,}["'']'
    }

    foreach ($relativePath in $trackedFiles) {
        $fileName = [IO.Path]::GetFileName($relativePath).ToLowerInvariant()
        if ($sensitiveNames -contains $fileName) {
            $errors.Add("sensitive filename is tracked: $relativePath")
        }

        $extension = [IO.Path]::GetExtension($relativePath).ToLowerInvariant()
        $isCMakeLists = [IO.Path]::GetFileName($relativePath) -eq 'CMakeLists.txt'
        if (($textExtensions -notcontains $extension) -and -not $isCMakeLists) {
            continue
        }

        $absolutePath = Join-Path $repositoryRoot $relativePath
        if (-not [IO.File]::Exists($absolutePath)) { continue }
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadLines($absolutePath)) {
            $lineNumber++
            foreach ($entry in $secretPatterns.GetEnumerator()) {
                if ($line -match $entry.Value) {
                    $errors.Add("possible $($entry.Key): ${relativePath}:$lineNumber")
                }
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Output "ERROR: $_" }
        throw "Security check failed with $($errors.Count) possible secret(s)."
    }

    Write-Output "Security secret check passed ($($trackedFiles.Count) tracked files)."
}
finally {
    Pop-Location
}

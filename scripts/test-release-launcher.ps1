[CmdletBinding()]
param(
    [string]$Executable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Executable) { $Executable = Join-Path $PSScriptRoot '..\release\embedded\Phasmophobia.exe' }
$source = (Get-Item -LiteralPath $Executable -ErrorAction Stop).FullName
$testParent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\out\release-launcher-tests'))
$fixture = Join-Path $testParent ([Guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture ('Unity ' + [char]0x6D4B + [char]0x8BD5)
$caller = Join-Path $fixture 'other working directory'
$data = Join-Path $game 'Phasmophobia_Data'
$metadata = Join-Path $data 'il2cpp_data\Metadata'
$originalTemp = $env:TEMP
$originalTmp = $env:TMP
[IO.Directory]::CreateDirectory($fixture) | Out-Null

try {
    foreach ($dir in @($caller, $metadata)) { [IO.Directory]::CreateDirectory($dir) | Out-Null }
    $env:TEMP = $fixture
    $env:TMP = $fixture
    . (Join-Path $PSScriptRoot 'Enter-MsvcEnvironment.ps1')

    $testDll = @'
#include <windows.h>

extern "C" __declspec(dllexport)
int WINAPI UnityMain2(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show) {
    if (instance != GetModuleHandleW(nullptr) || previous != nullptr) return 21;
    wchar_t expected[1024]{};
    if (!GetEnvironmentVariableW(L"TOOLSBOX_TEST_ARGUMENTS", expected, 1024) ||
        !commandLine || lstrcmpW(commandLine, expected) != 0) return 22;
    wchar_t directory[1024]{};
    if (!GetEnvironmentVariableW(L"TOOLSBOX_TEST_GAME_DIR", expected, 1024) ||
        !GetCurrentDirectoryW(1024, directory) || lstrcmpiW(directory, expected) != 0) return 23;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);
    if (show != ((startup.dwFlags & STARTF_USESHOWWINDOW) ? startup.wShowWindow : SW_SHOWDEFAULT)) return 24;
    HANDLE marker = CreateFileW(L"unity-entry.checked", GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker == INVALID_HANDLE_VALUE) return 25;
    CloseHandle(marker);
    return 73;
}
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'unity_test.cpp'), $testDll)
    Push-Location -LiteralPath $fixture
    try {
        & cl.exe /nologo /MT /LD unity_test.cpp /link /machine:x64 /out:UnityPlayer.dll
        if ($LASTEXITCODE -ne 0) { throw "Test DLL build failed ($LASTEXITCODE)." }
    } finally { Pop-Location }

    $engine = Join-Path $fixture 'UnityPlayer.dll'
    Copy-Item -LiteralPath $engine -Destination (Join-Path $game 'UnityPlayer.dll')
    Copy-Item -LiteralPath $engine -Destination (Join-Path $game 'GameAssembly.dll')
    [IO.File]::WriteAllBytes((Join-Path $metadata 'global-metadata.dat'), [byte[]]@(0))
    [IO.File]::WriteAllText((Join-Path $data 'boot.config'), '')
    $launcher = Join-Path $game 'Phasmophobia.exe'
    Copy-Item -LiteralPath $source -Destination $launcher

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $launcher
    $start.WorkingDirectory = $caller
    $start.UseShellExecute = $false
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.Arguments = ('-screen-fullscreen 0 -fixture "' + [char]0x5E26 + [char]0x7A7A + ' space"')
    $start.EnvironmentVariables['TOOLSBOX_TEST_ARGUMENTS'] = $start.Arguments
    $start.EnvironmentVariables['TOOLSBOX_TEST_GAME_DIR'] = $game
    foreach ($key in @('USERPROFILE', 'LOCALAPPDATA', 'APPDATA', 'TEMP', 'TMP')) {
        $start.EnvironmentVariables[$key] = $fixture
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(20000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'The fixture launcher timed out.'
        }
        if ($process.ExitCode -ne 73) {
            throw "Expected engine result 73, got $($process.ExitCode) (21=instance, 22=arguments, 23=working directory, 24=show mode, 25=marker)."
        }
        if (!(Test-Path -LiteralPath (Join-Path $game 'unity-entry.checked') -PathType Leaf)) {
            throw 'The fixture engine did not complete its checks.'
        }
    } finally { $process.Dispose() }
    Write-Host 'Passed: engine entry, wide arguments, Unicode installation path, working directory, and return code.'
} finally {
    $env:TEMP = $originalTemp
    $env:TMP = $originalTmp
    $checkedRoot = (Get-Item -LiteralPath $fixture).FullName
    if ([IO.Path]::GetDirectoryName($checkedRoot) -ne $testParent) { throw 'Unexpected fixture path during cleanup.' }
    Remove-Item -LiteralPath $checkedRoot -Recurse -Force
}

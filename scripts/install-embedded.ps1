param(
    [string]$GameExecutable = '',
    [switch]$PackageOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    if ([string]::IsNullOrWhiteSpace($GameExecutable)) {
        Write-Host '请选择 Steam 中的恐鬼症 → 管理 → 浏览本地文件，复制 Phasmophobia.exe 的完整路径。'
        $GameExecutable = Read-Host 'Phasmophobia.exe 路径 / Full path'
    }
    $GameExecutable = $GameExecutable.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($GameExecutable)) {
        throw '未输入游戏路径 / No game path provided.'
    }

    . (Join-Path $PSScriptRoot 'embed-game.ps1') -GameExecutable $GameExecutable -PackageOnly:$PackageOnly -SkipBuild
    $result = Invoke-EmbedGame -GameExecutable $GameExecutable -PackageOnly:$PackageOnly -SkipBuild
    if ($PackageOnly) {
        Write-Host '已生成嵌入候选，原游戏未修改 / Candidate generated; game unchanged.'
        Write-Host $result.EmbeddedExecutable
    }
    else {
        Write-Host '嵌入安装完成。请从 Steam 启动游戏，按 U 打开菜单。'
        Write-Host 'Installed. Launch from Steam and press U to open the menu.'
        Write-Host ('原版备份 / Original backup: ' + $result.Backup)
    }
    Write-Host ('文件清单 / Manifest: ' + $result.Manifest)
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

<#
删除 third-party/saba/.git（Saba 克隆残留的嵌套仓库）。

用法（建议先预演，再执行）：
    .\script\remove_saba_git.ps1            # 只显示将删除的路径，不删除
    .\script\remove_saba_git.ps1 -Apply     # 确认后真正删除

脚本会校验目标路径确实位于项目根目录下的 third-party/saba 内，
防止误删其它目录。
#>

[CmdletBinding()]
param(
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

# script/ 的上一级就是项目根目录
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SabaRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot 'third-party\saba')
)
$Target = [System.IO.Path]::GetFullPath(
    (Join-Path $SabaRoot '.git')
)

Write-Host "项目根目录 : $ProjectRoot"
Write-Host "目标路径   : $Target"

if (-not (Test-Path -LiteralPath $Target -PathType Container)) {
    Write-Host "目标不存在，无需处理。"
    exit 0
}

# 安全校验：目标必须是 third-party/saba 目录下的 .git 文件夹
$expectedPrefix = $SabaRoot.TrimEnd('\') + '\'
$leaf = Split-Path -Leaf $Target
if (-not $Target.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    $leaf -ne '.git') {
    Write-Error "目标路径校验失败，已中止：$Target"
    exit 1
}

Write-Host "校验通过：目标位于 third-party/saba 内且名称为 .git。"

if (-not $Apply) {
    Write-Host ""
    Write-Host "预演模式：未删除任何内容。"
    Write-Host "确认无误后执行：.\script\remove_saba_git.ps1 -Apply"
    exit 0
}

Write-Host "正在删除：$Target"
Remove-Item -LiteralPath $Target -Recurse -Force

if (Test-Path -LiteralPath $Target) {
    Write-Error "删除失败：目标仍然存在。"
    exit 1
}

Write-Host "删除完成。现在可以正常提交 third-party/saba 的源码了。"

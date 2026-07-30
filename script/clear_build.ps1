[CmdletBinding()]
param(
    [string]$BuildPath
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BuildPath)) {
    $BuildPath = Join-Path $ProjectRoot 'build'
}
elseif (-not [System.IO.Path]::IsPathRooted($BuildPath)) {
    $BuildPath = Join-Path $ProjectRoot $BuildPath
}

if (Test-Path -LiteralPath $BuildPath) {
    Remove-Item -LiteralPath $BuildPath -Recurse -Force -Confirm:$false
    Write-Host "已清除 build 文件夹：$BuildPath" -ForegroundColor Green
}
else {
    Write-Host "build 文件夹不存在：$BuildPath" -ForegroundColor Yellow
}

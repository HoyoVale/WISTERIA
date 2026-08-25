[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [string]$BuildPath,

    [string]$OutputDirectory,

    [string]$Version = '1.2.0',

    [string]$CMakePath
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildPath)) {
    $BuildPath = Join-Path $ProjectRoot 'build'
}
$BuildPath = [System.IO.Path]::GetFullPath($BuildPath)
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot 'artifacts/sdk'
}

if ([string]::IsNullOrWhiteSpace($CMakePath)) {
    $CMakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $CMakeCommand) {
        $CMakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    }
    if ($null -ne $CMakeCommand) {
        $CMakePath = $CMakeCommand.Source
    }
    else {
        throw '找不到 CMake，请通过 -CMakePath 指定路径。'
    }
}
if (-not (Test-Path -LiteralPath $CMakePath -PathType Leaf)) {
    throw "找不到 CMake：$CMakePath"
}

function Invoke-CMake {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $CMakePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 执行失败，退出码：$LASTEXITCODE"
    }
}

$Staging = Join-Path $BuildPath 'sdk-package'
Write-Host "正在生成 SDK 安装树到 $Staging..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $Staging) {
    Remove-Item -LiteralPath $Staging -Recurse -Force
}
Invoke-CMake @(
    '--install', $BuildPath,
    '--config', $Configuration,
    '--prefix', $Staging,
    '--component', 'wisteria-sdk'
)

# 打包前附加许可证和 SDK 文档。
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'LICENSE') `
    -Destination (Join-Path $Staging 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'docs/SDK.md') `
    -Destination (Join-Path $Staging 'SDK.md') -Force
Set-Content -LiteralPath (Join-Path $Staging 'WISTERIA_SDK_VERSION') `
    -Value $Version -Encoding ascii

if ($env:OS -eq 'Windows_NT') {
    $Platform = 'win64'
}
else {
    $Platform = 'linux-x86_64'
}
$PackageName = "wisteria-sdk-$Version-$Platform.zip"
$PackagePath = Join-Path $OutputDirectory $PackageName

Write-Host "正在打包 $PackageName ..." -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}
if (Test-Path -LiteralPath $PackagePath) {
    Remove-Item -LiteralPath $PackagePath -Force
}
Compress-Archive `
    -Path (Join-Path $Staging '*') `
    -DestinationPath $PackagePath `
    -CompressionLevel Optimal

$PackageSizeMb = [Math]::Round((Get-Item -LiteralPath $PackagePath).Length / 1MB, 2)
Write-Host "SDK 打包完成：" -ForegroundColor Green
Write-Host "  $PackagePath"
Write-Host "  大小：${PackageSizeMb} MB"

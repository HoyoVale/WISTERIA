[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [string]$BuildPath,

    [string]$CMakePath
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildPath)) {
    $BuildPath = Join-Path $ProjectRoot 'build'
}
$BuildPath = [System.IO.Path]::GetFullPath($BuildPath)

if ([string]::IsNullOrWhiteSpace($CMakePath)) {
    $CMakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $CMakeCommand) {
        $CMakePath = $CMakeCommand.Source
    }
    else {
        $CMakePath = 'C:\Program Files\CMake\bin\cmake.exe'
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

$InstallPrefix = Join-Path $BuildPath 'sdk-install'
$ConsumerSource = Join-Path $ProjectRoot 'tests/sdk_consumer'
$ConsumerBuild = Join-Path $BuildPath 'sdk-consumer'

Write-Host "正在安装 WISTERIA SDK 到 $InstallPrefix..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $InstallPrefix) {
    Remove-Item -LiteralPath $InstallPrefix -Recurse -Force
}
Invoke-CMake @(
    '--install', $BuildPath,
    '--config', $Configuration,
    '--prefix', $InstallPrefix,
    '--component', 'wisteria-sdk'
)

Write-Host "正在配置 SDK 消费测试..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $ConsumerBuild) {
    Remove-Item -LiteralPath $ConsumerBuild -Recurse -Force
}
Invoke-CMake @(
    '-S', $ConsumerSource,
    '-B', $ConsumerBuild,
    "-DCMAKE_PREFIX_PATH=$InstallPrefix"
)

Write-Host "正在编译 SDK 消费测试 [$Configuration]..." -ForegroundColor Cyan
Invoke-CMake @(
    '--build', $ConsumerBuild,
    '--config', $Configuration,
    '--parallel'
)

$ConsumerCandidates = @(
    (Join-Path $ConsumerBuild "$Configuration/wisteria_sdk_consumer.exe"),
    (Join-Path $ConsumerBuild 'wisteria_sdk_consumer.exe'),
    (Join-Path $ConsumerBuild "$Configuration/wisteria_sdk_consumer"),
    (Join-Path $ConsumerBuild 'wisteria_sdk_consumer')
)
$Consumer = $ConsumerCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($Consumer)) {
    throw "SDK 消费测试可执行文件未生成。已检查：$($ConsumerCandidates -join ', ')"
}

Write-Host "正在运行 SDK 消费测试..." -ForegroundColor Cyan
# Windows 上共享库位于 <prefix>/bin，运行消费者前先加入 PATH。
$InstallBin = Join-Path $InstallPrefix 'bin'
if (Test-Path -LiteralPath $InstallBin -PathType Container) {
    $env:PATH = "$InstallBin;$env:PATH"
}
& $Consumer
if ($LASTEXITCODE -ne 0) {
    throw "SDK 消费测试失败，退出码：$LASTEXITCODE"
}

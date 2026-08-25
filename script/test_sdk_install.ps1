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
# 运行消费者前把共享库目录加入运行时搜索路径。
if ($env:OS -eq 'Windows_NT') {
    $InstallBin = Join-Path $InstallPrefix 'bin'
    if (Test-Path -LiteralPath $InstallBin -PathType Container) {
        $env:PATH = "$InstallBin;$env:PATH"
    }
}
else {
    $InstallLib = Join-Path $InstallPrefix 'lib'
    if (Test-Path -LiteralPath $InstallLib -PathType Container) {
        $env:LD_LIBRARY_PATH = "$InstallLib`:$env:LD_LIBRARY_PATH"
    }
}
& $Consumer
if ($LASTEXITCODE -ne 0) {
    throw "SDK 消费测试失败，退出码：$LASTEXITCODE"
}

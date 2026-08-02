[CmdletBinding()]
param(
    [ValidateSet('configure', 'compile', 'test', 'all')]
    [string]$Action = 'all',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [string]$CMakePath
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $ProjectRoot 'build'

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
    throw "找不到 CMake：$CMakePath。请将 CMake 加入 PATH，或使用 -CMakePath 指定路径。"
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

function Configure-Project {
    Write-Host "正在配置项目 [$Configuration]..." -ForegroundColor Cyan

    Invoke-CMake @(
        '-S', $ProjectRoot,
        '-B', $BuildPath,
        "-DCMAKE_BUILD_TYPE=$Configuration"
    )
}

function Compile-Project {
    # Always reconfigure so switching Debug/Release/RelWithDebInfo cannot
    # silently reuse a single-config generator's previous CMAKE_BUILD_TYPE.
    Configure-Project

    Write-Host "正在编译项目 [$Configuration]..." -ForegroundColor Cyan

    # Visual Studio 是多配置生成器，必须明确指定配置；单配置生成器也
    # 会安全忽略 --config 之外的多配置语义。
    Invoke-CMake @(
        '--build', $BuildPath,
        '--config', $Configuration,
        '--parallel'
    )
}

function Test-Project {
    Compile-Project

    $CTestPath = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $CTestPath -PathType Leaf)) {
        throw "找不到 CTest：$CTestPath"
    }

    Write-Host "正在运行自动化测试 [$Configuration]..." -ForegroundColor Cyan
    $CTestArguments = @(
        '--test-dir', $BuildPath,
        '-C', $Configuration,
        '--output-on-failure'
    )
    & $CTestPath @CTestArguments

    if ($LASTEXITCODE -ne 0) {
        throw "自动化测试失败，退出码：$LASTEXITCODE"
    }
}

switch ($Action) {
    'configure' {
        Configure-Project
    }

    'compile' {
        Compile-Project
    }

    'test' {
        Test-Project
    }

    'all' {
        Compile-Project
    }
}

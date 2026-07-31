[CmdletBinding()]
param(
    [ValidateSet('configure', 'compile', 'test', 'all')]
    [string]$Action = 'all',

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
    Write-Host "正在配置项目..." -ForegroundColor Cyan

    Invoke-CMake @(
        '-S', $ProjectRoot,
        '-B', $BuildPath
    )
}

function Compile-Project {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildPath 'CMakeCache.txt'))) {
        Configure-Project
    }

    Write-Host "正在编译项目..." -ForegroundColor Cyan

    # Visual Studio 是多配置生成器，必须明确指定 Debug/Release。
    Invoke-CMake @(
        '--build', $BuildPath,
        '--config', 'Debug',
        '--parallel'
    )
}

function Test-Project {
    Compile-Project

    $CTestPath = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $CTestPath -PathType Leaf)) {
        throw "找不到 CTest：$CTestPath"
    }

    Write-Host "正在运行自动化测试..." -ForegroundColor Cyan
    $CTestArguments = @(
        '--test-dir', $BuildPath,
        '-C', 'Debug',
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
        Configure-Project
        Compile-Project
    }
}

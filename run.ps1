[CmdletBinding()]
param(
    [ValidateSet('run', 'build', 'compile', 'test', 'clean')]
    [string]$Action = 'run',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [string[]]$ApplicationArguments = @()
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = $PSScriptRoot
$BuildScript = Join-Path $ProjectRoot 'script/build.ps1'
$ClearScript = Join-Path $ProjectRoot 'script/clear_build.ps1'
$BuildPath = Join-Path $ProjectRoot 'build'
$ExecutableCandidates = @(
    (Join-Path $BuildPath "$Configuration/wisteria.exe"),
    (Join-Path $BuildPath 'wisteria.exe')
)

function Start-Application {
    $Executable = $ExecutableCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($Executable)) {
        throw "可执行文件未生成。已检查：$($ExecutableCandidates -join ', ')"
    }

    Write-Host "正在运行 WISTERIA [$Configuration]..." -ForegroundColor Green
    # 让程序的当前工作目录固定为项目根目录，便于使用相对资源路径。
    Push-Location $ProjectRoot
    try {
        & $Executable @ApplicationArguments

        if ($LASTEXITCODE -ne 0) {
            throw "程序运行失败，退出码：$LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

switch ($Action) {
    'clean' {
        & $ClearScript -BuildPath $BuildPath
    }

    'build' {
        & $ClearScript -BuildPath $BuildPath
        & $BuildScript -Action configure -Configuration $Configuration
    }

    'compile' {
        & $BuildScript -Action compile -Configuration $Configuration
        Start-Application
    }

    'test' {
        & $BuildScript -Action test -Configuration $Configuration
    }

    'run' {
        & $ClearScript -BuildPath $BuildPath
        & $BuildScript -Action all -Configuration $Configuration
        Start-Application
    }
}

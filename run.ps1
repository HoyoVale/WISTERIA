[CmdletBinding()]
param(
    [ValidateSet('run', 'build', 'compile', 'test', 'clean')]
    [string]$Action = 'run',

    [string[]]$ApplicationArguments = @()
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = $PSScriptRoot
$BuildScript = Join-Path $ProjectRoot 'script/build.ps1'
$ClearScript = Join-Path $ProjectRoot 'script/clear_build.ps1'
$BuildPath = Join-Path $ProjectRoot 'build'
$Executable = Join-Path $BuildPath 'Debug/wisteria.exe'

function Start-Application {
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "可执行文件未生成：$Executable"
    }

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
        & $BuildScript -Action configure
    }

    'compile' {
        & $BuildScript -Action compile
        Start-Application
    }

    'test' {
        & $BuildScript -Action test
    }

    'run' {
        & $ClearScript -BuildPath $BuildPath
        & $BuildScript -Action all
        Start-Application
    }
}
